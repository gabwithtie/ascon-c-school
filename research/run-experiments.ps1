param(
    [Parameter(Mandatory = $true)]
    [string] $OutputDirectory
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$output = [System.IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $output) {
    throw "Output directory already exists: $output"
}

$builds = @(
    @{ Label = 'O2'; Directory = 'build'; TestPreset = 'windows-v12-release' },
    @{ Label = 'O1'; Directory = 'build-size'; TestPreset = 'windows-v12-size-release' }
)
$algorithms = @('ascon128', 'ascon128a')
$implementations = @('ref', 'opt64')

Push-Location $root
try {
    foreach ($build in $builds) {
        $cache = Join-Path $root "$($build.Directory)\CMakeCache.txt"
        $releaseFlags = @(Select-String -Path $cache -Pattern '^CMAKE_C_FLAGS_RELEASE:STRING=').Line
        $targetFlags = @(Select-String -Path $cache -Pattern '^REL_FLAGS:STRING=').Line
        if ($releaseFlags -ne "CMAKE_C_FLAGS_RELEASE:STRING=/MD /$($build.Label) /Ob2 /DNDEBUG" -or
            $targetFlags -ne "REL_FLAGS:STRING=/$($build.Label)") {
            throw "Unexpected Release flags in $cache; reconfigure the preset before measuring."
        }
        foreach ($algorithm in $algorithms) {
            foreach ($implementation in $implementations) {
                $exe = Join-Path $root "$($build.Directory)\research\Release\research_bench_${algorithm}_${implementation}.exe"
                if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
                    throw "Build the $($build.Label) preset first; executable missing: $exe"
                }
            }
        }
        & ctest --preset $build.TestPreset
        if ($LASTEXITCODE -ne 0) { throw "Correctness or smoke tests failed for $($build.Label)." }
    }

    New-Item -ItemType Directory -Path $output | Out-Null
    $rawPath = Join-Path $output 'raw.csv'
    'algorithm,implementation,optimization,run,message_bytes,ad_bytes,batch,iterations,ticks,frequency,timer_overhead_ticks,affinity_mask' |
        Set-Content -LiteralPath $rawPath -Encoding UTF8

    $cpu = Get-CimInstance Win32_Processor | Select-Object -First 1 Name, NumberOfCores, NumberOfLogicalProcessors
    $os = Get-CimInstance Win32_OperatingSystem | Select-Object Caption, Version, OSArchitecture
    $commit = (& git rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0) { throw 'Unable to identify source commit.' }
    $cmakeVersionLines = @(& cmake --version)
    if ($LASTEXITCODE -ne 0) { throw 'Unable to identify the CMake version.' }
    $cmakeVersion = $cmakeVersionLines[0]
    $powerPlan = & powercfg /getactivescheme
    if ($LASTEXITCODE -ne 0) { throw 'Unable to identify the active power plan.' }
    $buildDetails = foreach ($build in $builds) {
        $cache = Join-Path $root "$($build.Directory)\CMakeCache.txt"
        $compilerInfo = Get-ChildItem (Join-Path $root "$($build.Directory)\CMakeFiles") -Recurse -Filter CMakeCCompiler.cmake |
            Select-Object -First 1
        if (-not $compilerInfo) { throw "CMake compiler metadata missing in $($build.Directory)." }
        [ordered]@{
            optimization = $build.Label
            cache_flags = @(Select-String -Path $cache -Pattern '^(CMAKE_C_FLAGS_RELEASE|REL_FLAGS|CMAKE_GENERATOR):' |
                ForEach-Object { $_.Line })
            compiler = @(Select-String -Path $compilerInfo.FullName -Pattern '^set\(CMAKE_C_COMPILER(_ID|_VERSION)? ' |
                ForEach-Object { $_.Line })
        }
    }
    $metadata = [ordered]@{
        started_utc = [DateTime]::UtcNow.ToString('o')
        source_commit = $commit
        source_hashes = @(Get-FileHash (Join-Path $PSScriptRoot 'benchmark.c'),
            (Join-Path $PSScriptRoot 'correctness.c'), (Join-Path $root 'CMakeLists.txt'),
            (Join-Path $PSScriptRoot 'CMakeLists.txt'), (Join-Path $root 'CMakePresets.json'),
            (Join-Path $PSScriptRoot 'run-experiments.ps1'), (Join-Path $PSScriptRoot 'summarize.ps1') -Algorithm SHA256 |
            ForEach-Object { [ordered]@{ file = $_.Path.Substring($root.Length + 1); sha256 = $_.Hash } })
        cpu = $cpu
        os = $os
        cmake = $cmakeVersion
        power_plan = ($powerPlan | Out-String).Trim()
        background_load = 'Not recorded; machine was not exclusively reserved.'
        builds = @($buildDetails)
        timer = 'Windows QueryPerformanceCounter; ticks are not CPU cycles'
        inputs = 'Deterministic benchmark-only inputs; nonce is reused within timing batches.'
        runs = 3
        batches_per_case = 30
        test_presets_passed = @($builds | ForEach-Object { $_.TestPreset })
    }
    $metadata | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $output 'environment.json') -Encoding UTF8

    for ($run = 1; $run -le 3; $run++) {
        foreach ($build in $builds) {
            foreach ($algorithm in $algorithms) {
                foreach ($implementation in $implementations) {
                    $exe = Join-Path $root "$($build.Directory)\research\Release\research_bench_${algorithm}_${implementation}.exe"
                    Write-Host "Run $run`: $($build.Label) $algorithm $implementation"
                    $rows = @(& $exe $run)
                    if ($LASTEXITCODE -ne 0) { throw "Benchmark failed: $exe run $run." }
                    if ($rows.Count -ne 1020) { throw "Expected 1020 rows from $exe; got $($rows.Count)." }
                    $rows | Add-Content -LiteralPath $rawPath -Encoding UTF8
                }
            }
        }
    }
    & (Join-Path $PSScriptRoot 'summarize.ps1') -RawPath $rawPath -OutputPath (Join-Path $output 'summary.csv')
} finally {
    Pop-Location
}
