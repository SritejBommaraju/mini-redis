# Build script for Mini-Redis
# Automatically stops running mini_redis.exe processes before building
# to prevent linker errors (LNK1168: cannot open mini_redis.exe for writing)

param(
    [switch]$Force,          # Force kill processes without confirmation
    [string]$Config = "Debug", # Build configuration (Debug or Release)
    [string]$Generator = "NMake Makefiles"  # CMake generator
)

$ErrorActionPreference = "Stop"

Write-Host "Mini-Redis Build Script" -ForegroundColor Cyan
Write-Host "======================" -ForegroundColor Cyan
Write-Host ""

# Check for running mini_redis.exe processes
$processes = Get-Process -Name "mini_redis" -ErrorAction SilentlyContinue

if ($processes) {
    Write-Host "Found $($processes.Count) running mini_redis.exe process(es)" -ForegroundColor Yellow
    
    if ($Force) {
        Write-Host "Force flag set - stopping processes..." -ForegroundColor Yellow
        Stop-Process -Name "mini_redis" -Force -ErrorAction SilentlyContinue
        Start-Sleep -Milliseconds 500  # Give processes time to terminate
        Write-Host "Processes stopped." -ForegroundColor Green
    } else {
        $response = Read-Host "Stop running processes? (Y/N)"
        if ($response -eq "Y" -or $response -eq "y") {
            Stop-Process -Name "mini_redis" -Force -ErrorAction SilentlyContinue
            Start-Sleep -Milliseconds 500
            Write-Host "Processes stopped." -ForegroundColor Green
        } else {
            Write-Host "Build cancelled. Please stop mini_redis.exe manually and try again." -ForegroundColor Red
            exit 1
        }
    }
} else {
    Write-Host "No running mini_redis.exe processes found." -ForegroundColor Green
}

Write-Host ""

# Ensure build directory exists
if (-not (Test-Path "build")) {
    Write-Host "Creating build directory..." -ForegroundColor Cyan
    New-Item -ItemType Directory -Path "build" | Out-Null
}

# Change to build directory
Push-Location build

try {
    # Configure CMake if needed
    if (-not (Test-Path "CMakeCache.txt")) {
        Write-Host "Configuring CMake..." -ForegroundColor Cyan
        cmake -G $Generator ..
        if ($LASTEXITCODE -ne 0) {
            Write-Host "CMake configuration failed!" -ForegroundColor Red
            exit 1
        }
    }
    
    # Build the project
    Write-Host "Building project (Configuration: $Config)..." -ForegroundColor Cyan
    
    if ($Generator -eq "NMake Makefiles") {
        nmake
    } else {
        cmake --build . --config $Config
    }
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Build failed!" -ForegroundColor Red
        exit 1
    }
    
    Write-Host ""
    Write-Host "Build completed successfully!" -ForegroundColor Green
    Write-Host "Executable: build\mini_redis.exe" -ForegroundColor Green
    
} catch {
    Write-Host "Error during build: $_" -ForegroundColor Red
    exit 1
} finally {
    Pop-Location
}

