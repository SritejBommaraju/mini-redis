# PowerShell script to build and start Mini-Redis server
# Usage: .\start_server.ps1 [port]

param(
    [int]$Port = 6379
)

Write-Host "Mini-Redis Server Startup Script" -ForegroundColor Green
Write-Host "================================" -ForegroundColor Green

# Check if build directory exists
if (-not (Test-Path "build")) {
    Write-Host "Creating build directory..." -ForegroundColor Yellow
    New-Item -ItemType Directory -Path "build" | Out-Null
}

# Change to build directory
Push-Location build

try {
    # Configure CMake (Release build)
    Write-Host "Configuring CMake (Release build)..." -ForegroundColor Yellow
    cmake .. -DCMAKE_BUILD_TYPE=Release
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host "CMake configuration failed!" -ForegroundColor Red
        exit 1
    }

    # Build the project
    Write-Host "Building project..." -ForegroundColor Yellow
    cmake --build . --config Release
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Build failed!" -ForegroundColor Red
        exit 1
    }

    Write-Host "Build successful!" -ForegroundColor Green
    Write-Host "Starting server on port $Port..." -ForegroundColor Green
    Write-Host "Press Ctrl+C to stop the server" -ForegroundColor Yellow
    Write-Host ""

    # Run the server
    .\Release\mini_redis.exe
    # Note: In a real implementation, you might want to pass the port as an argument
    # For now, the server always uses port 6379

} catch {
    Write-Host "Error: $_" -ForegroundColor Red
    exit 1
} finally {
    Pop-Location
}

