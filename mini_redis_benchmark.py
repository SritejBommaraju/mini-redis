#!/usr/bin/env python3
"""
Mini-Redis Benchmark and Validation Script
Tests and benchmarks the Mini-Redis C++ server using RESP protocol.
"""

import asyncio
import time
import csv
import random
import base64
import sys
from typing import List, Any, Optional, Tuple
from datetime import datetime

# ANSI color codes (with Windows fallback)
try:
    import colorama
    colorama.init()
    GREEN = '\033[92m'
    RED = '\033[91m'
    YELLOW = '\033[93m'
    BLUE = '\033[94m'
    RESET = '\033[0m'
    BOLD = '\033[1m'
except ImportError:
    GREEN = RED = YELLOW = BLUE = RESET = BOLD = ''

# Configuration
SERVER_HOST = 'localhost'
SERVER_PORT = 6379
CONNECTION_TIMEOUT = 5.0
CONNECTION_RETRY_DELAY = 0.5
MAX_CONNECTION_RETRIES = 10
SINGLE_CLIENT_OPS = 50_000
MULTI_CLIENT_COUNT = 5
MULTI_CLIENT_OPS_PER_CLIENT = 20_000


# ============================================================================
# Utility Functions
# ============================================================================

def random_hex_key(length: int = 16) -> str:
    """Generate a random hexadecimal key."""
    return ''.join(random.choices('0123456789abcdef', k=length))


def random_bytes_value(size: int = 100) -> bytes:
    """Generate random binary data."""
    return bytes(random.randint(0, 255) for _ in range(size))


def format_bytes(b: bytes, max_display: int = 50) -> str:
    """Format bytes for display (base64 encoded)."""
    if len(b) <= max_display:
        return base64.b64encode(b).decode('ascii')
    return base64.b64encode(b[:max_display]).decode('ascii') + '...'


# ============================================================================
# RESP Protocol Encoding/Decoding
# ============================================================================

def encode_array(items: List[str]) -> bytes:
    """
    Encode a list of strings as a RESP array.
    Format: *<count>\r\n followed by bulk strings $<len>\r\n<value>\r\n
    """
    result = f"*{len(items)}\r\n".encode('utf-8')
    for item in items:
        if isinstance(item, bytes):
            item_bytes = item
        else:
            item_bytes = item.encode('utf-8')
        result += f"${len(item_bytes)}\r\n".encode('utf-8')
        result += item_bytes
        result += b"\r\n"
    return result


async def decode_resp(reader: asyncio.StreamReader) -> Any:
    """
    Decode a RESP response from the stream.
    Handles: simple strings (+), bulk strings ($), errors (-), integers (:), arrays (*)
    """
    # Read the first byte to determine type
    first_byte = await reader.read(1)
    if not first_byte:
        raise ConnectionError("Connection closed by server")
    
    prefix = first_byte[0]
    
    if prefix == ord('+'):
        # Simple string
        line = await reader.readuntil(b'\r\n')
        return line[:-2].decode('utf-8', errors='replace')
    
    elif prefix == ord('-'):
        # Error
        line = await reader.readuntil(b'\r\n')
        error_msg = line[:-2].decode('utf-8', errors='replace')
        raise RuntimeError(f"Server error: {error_msg}")
    
    elif prefix == ord(':'):
        # Integer
        line = await reader.readuntil(b'\r\n')
        return int(line[:-2].decode('utf-8'))
    
    elif prefix == ord('$'):
        # Bulk string
        length_line = await reader.readuntil(b'\r\n')
        length_str = length_line[:-2].decode('utf-8')
        
        if length_str == '-1':
            # Nil bulk string
            return None
        
        length = int(length_str)
        data = await reader.readexactly(length)
        # Read trailing \r\n
        await reader.readuntil(b'\r\n')
        return data.decode('utf-8', errors='replace')
    
    elif prefix == ord('*'):
        # Array
        count_line = await reader.readuntil(b'\r\n')
        count = int(count_line[:-2].decode('utf-8'))
        result = []
        for _ in range(count):
            result.append(await decode_resp(reader))
        return result
    
    else:
        raise ValueError(f"Unknown RESP prefix: {chr(prefix)}")


# ============================================================================
# MiniRedisClient Class
# ============================================================================

class MiniRedisClient:
    """Async Redis client using RESP protocol."""
    
    def __init__(self, host: str = SERVER_HOST, port: int = SERVER_PORT):
        self.host = host
        self.port = port
        self.reader: Optional[asyncio.StreamReader] = None
        self.writer: Optional[asyncio.StreamWriter] = None
        self.connected = False
    
    async def connect(self, retries: int = MAX_CONNECTION_RETRIES) -> bool:
        """Connect to the server with retry logic."""
        for attempt in range(retries):
            try:
                self.reader, self.writer = await asyncio.wait_for(
                    asyncio.open_connection(self.host, self.port),
                    timeout=CONNECTION_TIMEOUT
                )
                self.connected = True
                return True
            except (ConnectionRefusedError, OSError, asyncio.TimeoutError) as e:
                if attempt < retries - 1:
                    await asyncio.sleep(CONNECTION_RETRY_DELAY)
                else:
                    print(f"{RED}Failed to connect after {retries} attempts: {e}{RESET}")
                    return False
        return False
    
    async def execute(self, command: str, *args: str) -> Any:
        """Execute a command and return the response."""
        if not self.connected or not self.writer:
            raise RuntimeError("Not connected to server")
        
        # Build command array
        cmd_items = [command] + list(args)
        request = encode_array(cmd_items)
        
        # Send request
        self.writer.write(request)
        await self.writer.drain()
        
        # Read response with timeout
        try:
            response = await asyncio.wait_for(
                decode_resp(self.reader),
                timeout=CONNECTION_TIMEOUT
            )
            return response
        except asyncio.TimeoutError:
            raise RuntimeError("Command timeout")
    
    async def close(self):
        """Close the connection."""
        if self.writer:
            self.writer.close()
            await self.writer.wait_closed()
        self.connected = False
        self.reader = None
        self.writer = None


# ============================================================================
# Correctness Tests
# ============================================================================

async def test_ping(client: MiniRedisClient) -> Tuple[str, bool]:
    """Test PING command."""
    try:
        result = await client.execute("PING")
        if result == "PONG":
            return ("PING", True)
        return ("PING", False)
    except Exception as e:
        print(f"{RED}  Error: {e}{RESET}")
        return ("PING", False)


async def test_set_get(client: MiniRedisClient) -> Tuple[str, bool]:
    """Test SET and GET commands."""
    try:
        key = random_hex_key()
        value = f"test_value_{random.randint(1000, 9999)}"
        
        # SET
        set_result = await client.execute("SET", key, value)
        if set_result != "OK":
            return ("SET/GET", False)
        
        # GET
        get_result = await client.execute("GET", key)
        if get_result == value:
            return ("SET/GET", True)
        return ("SET/GET", False)
    except Exception as e:
        print(f"{RED}  Error: {e}{RESET}")
        return ("SET/GET", False)


async def test_del(client: MiniRedisClient) -> Tuple[str, bool]:
    """Test DEL command."""
    try:
        key = random_hex_key()
        value = "test_value"
        
        # SET
        await client.execute("SET", key, value)
        
        # DEL
        del_result = await client.execute("DEL", key)
        if del_result != 1:
            return ("DEL", False)
        
        # Verify deleted
        get_result = await client.execute("GET", key)
        if get_result is None:
            return ("DEL", True)
        return ("DEL", False)
    except Exception as e:
        print(f"{RED}  Error: {e}{RESET}")
        return ("DEL", False)


async def test_overwrite(client: MiniRedisClient) -> Tuple[str, bool]:
    """Test overwriting a key."""
    try:
        key = random_hex_key()
        value1 = "first_value"
        value2 = "second_value"
        
        # SET first value
        await client.execute("SET", key, value1)
        
        # SET second value (overwrite)
        await client.execute("SET", key, value2)
        
        # GET should return second value
        result = await client.execute("GET", key)
        if result == value2:
            return ("OVERWRITE", True)
        return ("OVERWRITE", False)
    except Exception as e:
        print(f"{RED}  Error: {e}{RESET}")
        return ("OVERWRITE", False)


async def test_binary_safe(client: MiniRedisClient) -> Tuple[str, bool]:
    """Test binary-safe value storage."""
    try:
        key = random_hex_key()
        # Generate random bytes (including null bytes and non-UTF8 sequences)
        value_bytes = random_bytes_value(200)
        # Convert to base64 for transmission as string
        value_str = base64.b64encode(value_bytes).decode('ascii')
        
        # SET
        await client.execute("SET", key, value_str)
        
        # GET
        result = await client.execute("GET", key)
        if result == value_str:
            # Decode and verify binary match
            result_bytes = base64.b64decode(result)
            if result_bytes == value_bytes:
                return ("BINARY_SAFE", True)
        return ("BINARY_SAFE", False)
    except Exception as e:
        print(f"{RED}  Error: {e}{RESET}")
        return ("BINARY_SAFE", False)


async def run_correctness_tests(client: MiniRedisClient) -> List[Tuple[str, bool]]:
    """Run all correctness tests."""
    print(f"\n{BOLD}{BLUE}=== Correctness Tests ==={RESET}\n")
    
    tests = [
        test_ping,
        test_set_get,
        test_del,
        test_overwrite,
        test_binary_safe,
    ]
    
    results = []
    for test_func in tests:
        test_name, passed = await test_func(client)
        results.append((test_name, passed))
        
        status = f"{GREEN}PASS{RESET}" if passed else f"{RED}FAIL{RESET}"
        print(f"  {test_name:20s} ... {status}")
    
    passed_count = sum(1 for _, p in results if p)
    total_count = len(results)
    print(f"\n{BOLD}Results: {passed_count}/{total_count} tests passed{RESET}\n")
    
    return results


# ============================================================================
# Benchmark Functions
# ============================================================================

async def benchmark_single_client(client: MiniRedisClient, num_ops: int = SINGLE_CLIENT_OPS) -> dict:
    """Run single-client benchmark."""
    print(f"{BOLD}{BLUE}=== Single-Client Benchmark ==={RESET}")
    print(f"Running {num_ops:,} operations sequentially...")
    
    # Generate keys upfront
    keys = [random_hex_key() for _ in range(num_ops // 2)]
    values = [f"value_{i}" for i in range(num_ops // 2)]
    
    start_time = time.perf_counter()
    ops_done = 0
    errors = 0
    
    # 50% SET, 50% GET
    for i in range(num_ops):
        try:
            if i % 2 == 0:
                # SET operation
                key_idx = i // 2
                await client.execute("SET", keys[key_idx], values[key_idx])
            else:
                # GET operation
                key_idx = (i - 1) // 2
                await client.execute("GET", keys[key_idx])
            ops_done += 1
        except Exception:
            errors += 1
    
    end_time = time.perf_counter()
    total_time = end_time - start_time
    qps = ops_done / total_time if total_time > 0 else 0
    
    print(f"  Operations: {ops_done:,}")
    print(f"  Errors: {errors}")
    print(f"  Total time: {total_time:.3f}s")
    print(f"  QPS: {qps:,.0f}\n")
    
    return {
        'test_name': 'single_client',
        'operations': ops_done,
        'total_time_sec': total_time,
        'qps': qps,
        'clients': 1,
        'ops_per_client': ops_done,
        'errors': errors,
    }


async def benchmark_client(client: MiniRedisClient, client_id: int, num_ops: int) -> dict:
    """Run benchmark for a single client in multi-client scenario."""
    keys = [random_hex_key() for _ in range(num_ops // 2)]
    values = [f"value_{client_id}_{i}" for i in range(num_ops // 2)]
    
    start_time = time.perf_counter()
    ops_done = 0
    errors = 0
    
    for i in range(num_ops):
        try:
            if i % 2 == 0:
                key_idx = i // 2
                await client.execute("SET", keys[key_idx], values[key_idx])
            else:
                key_idx = (i - 1) // 2
                await client.execute("GET", keys[key_idx])
            ops_done += 1
        except Exception:
            errors += 1
    
    end_time = time.perf_counter()
    total_time = end_time - start_time
    
    return {
        'client_id': client_id,
        'operations': ops_done,
        'total_time_sec': total_time,
        'errors': errors,
    }


async def benchmark_multi_client(num_clients: int = MULTI_CLIENT_COUNT, 
                                 ops_per_client: int = MULTI_CLIENT_OPS_PER_CLIENT) -> dict:
    """Run multi-client concurrent benchmark."""
    print(f"{BOLD}{BLUE}=== Multi-Client Benchmark ==={RESET}")
    print(f"Running {num_clients} clients × {ops_per_client:,} ops each concurrently...")
    
    # Create and connect all clients
    clients = []
    for i in range(num_clients):
        client = MiniRedisClient()
        if not await client.connect():
            print(f"{RED}Failed to connect client {i}{RESET}")
            continue
        clients.append(client)
    
    if not clients:
        print(f"{RED}No clients connected!{RESET}\n")
        return {
            'test_name': 'multi_client',
            'operations': 0,
            'total_time_sec': 0,
            'qps': 0,
            'clients': 0,
            'ops_per_client': 0,
            'errors': 0,
        }
    
    # Run all clients concurrently
    start_time = time.perf_counter()
    tasks = [benchmark_client(clients[i], i, ops_per_client) for i in range(len(clients))]
    results = await asyncio.gather(*tasks)
    end_time = time.perf_counter()
    
    # Close all clients
    for client in clients:
        await client.close()
    
    # Aggregate results
    total_ops = sum(r['operations'] for r in results)
    total_errors = sum(r['errors'] for r in results)
    total_time = end_time - start_time
    qps = total_ops / total_time if total_time > 0 else 0
    
    print(f"  Clients: {len(clients)}")
    print(f"  Total operations: {total_ops:,}")
    print(f"  Total errors: {total_errors}")
    print(f"  Total time: {total_time:.3f}s")
    print(f"  Aggregate QPS: {qps:,.0f}")
    print(f"  Per-client QPS: {qps / len(clients):,.0f}\n")
    
    return {
        'test_name': 'multi_client',
        'operations': total_ops,
        'total_time_sec': total_time,
        'qps': qps,
        'clients': len(clients),
        'ops_per_client': ops_per_client,
        'errors': total_errors,
    }


# ============================================================================
# CSV Output
# ============================================================================

def write_csv_results(results: List[dict], filename: str = 'benchmark_results.csv'):
    """Write benchmark results to CSV file."""
    if not results:
        return
    
    file_exists = False
    try:
        with open(filename, 'r'):
            file_exists = True
    except FileNotFoundError:
        pass
    
    with open(filename, 'a', newline='') as f:
        fieldnames = ['test_name', 'operations', 'total_time_sec', 'qps', 'clients', 
                     'ops_per_client', 'errors', 'timestamp']
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        
        if not file_exists:
            writer.writeheader()
        
        for result in results:
            row = result.copy()
            row['timestamp'] = datetime.now().isoformat()
            writer.writerow(row)
    
    print(f"{GREEN}Results written to {filename}{RESET}\n")


def print_summary_table(results: List[dict]):
    """Print a pretty summary table of benchmark results."""
    print(f"{BOLD}{BLUE}=== Benchmark Summary ==={RESET}\n")
    print(f"{'Test':<20} {'Ops':<12} {'Time (s)':<12} {'QPS':<15} {'Clients':<10}")
    print("-" * 75)
    
    for result in results:
        test_name = result.get('test_name', 'unknown')
        ops = result.get('operations', 0)
        time_sec = result.get('total_time_sec', 0)
        qps = result.get('qps', 0)
        clients = result.get('clients', 1)
        
        print(f"{test_name:<20} {ops:>11,} {time_sec:>11.3f} {qps:>14,.0f} {clients:>9}")
    
    print()


# ============================================================================
# Main Function
# ============================================================================

async def main():
    """Main function to run all tests and benchmarks."""
    print(f"{BOLD}{GREEN}")
    print("=" * 60)
    print("  Mini-Redis Benchmark and Validation Script")
    print("=" * 60)
    print(f"{RESET}")
    
    # Connect to server
    print(f"Connecting to {SERVER_HOST}:{SERVER_PORT}...")
    client = MiniRedisClient()
    
    if not await client.connect():
        print(f"{RED}Failed to connect to server. Make sure it's running on {SERVER_HOST}:{SERVER_PORT}{RESET}")
        sys.exit(1)
    
    print(f"{GREEN}Connected!{RESET}\n")
    
    benchmark_results = []
    
    try:
        # Run correctness tests
        correctness_results = await run_correctness_tests(client)
        
        # Check if all tests passed
        all_passed = all(passed for _, passed in correctness_results)
        if not all_passed:
            print(f"{YELLOW}Warning: Some correctness tests failed. Continuing with benchmarks...{RESET}\n")
        
        # Run single-client benchmark
        single_result = await benchmark_single_client(client)
        benchmark_results.append(single_result)
        
        # Close client before multi-client benchmark
        await client.close()
        
        # Run multi-client benchmark
        multi_result = await benchmark_multi_client()
        benchmark_results.append(multi_result)
        
        # Write CSV
        write_csv_results(benchmark_results)
        
        # Print summary
        print_summary_table(benchmark_results)
        
        print(f"{BOLD}{GREEN}Benchmark complete!{RESET}\n")
        
    except KeyboardInterrupt:
        print(f"\n{YELLOW}Benchmark interrupted by user{RESET}")
        await client.close()
        sys.exit(1)
    except Exception as e:
        print(f"{RED}Unexpected error: {e}{RESET}")
        await client.close()
        sys.exit(1)


if __name__ == "__main__":
    asyncio.run(main())

