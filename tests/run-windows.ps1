$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$CC = if ($env:CC) { $env:CC } else { "cl" }

function Assert-LastExitCode([string]$Operation) {
    if ($LASTEXITCODE -ne 0) {
        throw "$Operation failed with exit code $LASTEXITCODE"
    }
}

function Invoke-Capture {
    param(
        [Parameter(Mandatory = $true)][string]$Executable,
        [string[]]$Arguments = @(),
        [Parameter(Mandatory = $true)][string]$OutputFile
    )

    $output = & $Executable @Arguments 2>&1
    $exitCode = $LASTEXITCODE
    $output | Out-File -FilePath $OutputFile -Encoding utf8
    if ($exitCode -ne 0) {
        throw "$Executable $($Arguments -join ' ') failed with exit code $exitCode"
    }
    return ($output -join "`n")
}

function Remove-TestArtifacts {
    Remove-Item -Force -ErrorAction SilentlyContinue `
        "$Root\tests\api_smoke.exe", `
        "$Root\tests\api_smoke.obj", `
        "$Root\tests\include_smoke.obj", `
        "$Root\tests\include_smoke_cpp.obj", `
        "$Root\tests\project\cbuild.exe", `
        "$Root\tests\project\cbuild.exe.old", `
        "$Root\tests\project\build.obj", `
        "$Root\example\cbuild.exe", `
        "$Root\example\cbuild.exe.old", `
        "$Root\example\build.obj", `
        "$Root\example\lib\cbuild.exe", `
        "$Root\example\lib\cbuild.exe.old", `
        "$Root\example\lib\build.obj"
    Remove-Item -Force -ErrorAction SilentlyContinue `
        "$Root\tests\project\*.log", "$Root\example\*.log"
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue `
        "$Root\tests\api-build", `
        "$Root\tests\project\build", `
        "$Root\example\build", `
        "$Root\example\lib\build"
}

try {
    Remove-TestArtifacts
    Set-Location $Root

    & $CC /nologo /std:c11 /W4 /D_CRT_SECURE_NO_WARNINGS `
        /c tests\include_smoke.c /Fo:tests\include_smoke.obj
    Assert-LastExitCode "C declaration-only compile"

    & $CC /nologo /std:c++14 /W4 /D_CRT_SECURE_NO_WARNINGS `
        /TP /c tests\include_smoke.cpp /Fo:tests\include_smoke_cpp.obj
    Assert-LastExitCode "C++ declaration-only compile"

    & $CC /nologo /std:c11 /W4 /D_CRT_SECURE_NO_WARNINGS `
        tests\api_smoke.c /Fo:tests\api_smoke.obj /Fe:tests\api_smoke.exe
    Assert-LastExitCode "API smoke compile"
    & .\tests\api_smoke.exe
    Assert-LastExitCode "API smoke test"

    Set-Location "$Root\tests\project"
    & $CC /nologo /std:c11 /W4 /D_CRT_SECURE_NO_WARNINGS `
        build.c /Fe:cbuild.exe
    Assert-LastExitCode "Integration build-program compile"

    $version = Invoke-Capture .\cbuild.exe @("--version") "build-version.log"
    if ($version.Trim() -ne "v0.1.0") { throw "Unexpected --version output: $version" }
    $null = Invoke-Capture .\cbuild.exe @("--help") "build-help.log"
    $list = Invoke-Capture .\cbuild.exe @("--list") "build-list.log"
    if ($list -notmatch "test_app" -or $list -notmatch "shared" -or
        $list -notmatch "generated_header") {
        throw "Target list is incomplete"
    }

    $graph = Invoke-Capture .\cbuild.exe @("--graph") "build-graph.log"
    if ($graph -notmatch "math" -or $graph -notmatch "generated_header") {
        throw "Build graph is incomplete"
    }

    $deps = Invoke-Capture .\cbuild.exe @("--deps=math") "build-deps.log"
    if ($deps -notmatch "test_app") { throw "Reverse dependencies are incomplete" }

    $manifest = Invoke-Capture .\cbuild.exe @("--manifest") "build-manifest.log"
    if ($manifest -notmatch "static_lib math" -or
        $manifest -notmatch "shared_lib shared" -or
        $manifest -notmatch "executable test_app") {
        throw "Manifest is incomplete"
    }

    $null = Invoke-Capture .\cbuild.exe @("--compile-commands", "-j2") "first-build.log"
    @(
        "build\generated.h",
        "build\command-dependency.txt",
        "build\command-main.txt",
        "build\test_app.exe",
        "build\math.lib",
        "build\shared.dll",
        "build\compile_commands.json"
    ) | ForEach-Object {
        if (-not (Test-Path $_)) { throw "Expected output is missing: $_" }
    }

    $appOutput = Invoke-Capture .\build\test_app.exe @("--self-test") "app-output.log"
    if ($appOutput -notmatch "sum=5 product=12 generated=7 feature=0") {
        throw "Executable output is incorrect"
    }

    $commands = @(Get-Content build\compile_commands.json -Raw | ConvertFrom-Json)
    if ($commands.Count -lt 4) { throw "compile_commands.json is incomplete" }
    foreach ($entry in $commands) {
        if (-not $entry.arguments -or $entry.arguments.Count -eq 0) {
            throw "compile_commands.json entry has no arguments"
        }
    }

    $noop = Invoke-Capture .\cbuild.exe @("-j2") "noop-build.log"
    if ($noop -match "\b(COMPILE|LINK)\s") {
        throw "No-op build unexpectedly compiled or linked a target"
    }

    $feature = Invoke-Capture .\cbuild.exe @("--feature", "--target=test_app") "feature-build.log"
    if ($feature -notmatch "COMPILE") { throw "Flag change did not rebuild test_app" }
    $featureOutput = Invoke-Capture .\build\test_app.exe @("--self-test") "feature-output.log"
    if ($featureOutput -notmatch "feature=1") { throw "Feature define was not applied" }

    $subcommand = Invoke-Capture .\cbuild.exe @("--feature", "--run=app") "subcommand.log"
    if ($subcommand -notmatch "feature=1") { throw "Subcommand did not run test_app" }

    Start-Sleep -Seconds 2
    (Get-Item inputs\schema.txt).LastWriteTime = Get-Date
    $generated = Invoke-Capture .\cbuild.exe @("--target=test_app") "generated-rebuild.log"
    if ($generated -notmatch "FILE_DEP" -or $generated -notmatch "COMPILE") {
        throw "Generated dependency did not rebuild its consumer"
    }

    Start-Sleep -Seconds 2
    (Get-Item build.c).LastWriteTime = Get-Date
    $selfRebuild = Invoke-Capture .\cbuild.exe @("--list") "self-rebuild.log"
    if ($selfRebuild -notmatch "Detected changes") {
        throw "Build program did not rebuild itself"
    }

    $null = Invoke-Capture .\cbuild.exe @("--clean") "clean.log"
    if (Test-Path build\test_app.exe) { throw "Clean left test_app.exe behind" }

    Set-Location "$Root\example"
    & $CC /nologo /std:c11 /W4 /D_CRT_SECURE_NO_WARNINGS `
        build.c /Fe:cbuild.exe
    Assert-LastExitCode "Example build-program compile"
    $null = Invoke-Capture .\cbuild.exe @() "example-build.log"
    $example = Invoke-Capture .\cbuild.exe @("--run=run") "example-run.log"
    if ($example -notmatch "2 \+ 3 = 5") { throw "Example output is incorrect" }
    $null = Invoke-Capture .\cbuild.exe @("--clean") "example-clean.log"
}
finally {
    Set-Location $Root
    Remove-TestArtifacts
}
