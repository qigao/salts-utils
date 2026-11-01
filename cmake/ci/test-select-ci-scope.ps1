$ErrorActionPreference = 'Stop'
$selector = Join-Path $PSScriptRoot 'select-ci-scope.ps1'
$temp = Join-Path ([System.IO.Path]::GetTempPath()) ("salts-utils-ci-scope-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temp | Out-Null

function Invoke-Git([string[]]$Arguments) {
  $result = & git -C $temp @Arguments
  if ($LASTEXITCODE -ne 0) { throw "git $($Arguments -join ' ') failed" }
  return (($result | Out-String).Trim())
}
function Assert-Output([string]$Output, [string]$Key, [string]$Expected, [string]$CaseName) {
  if ($Output -notmatch "(?m)^$([regex]::Escape($Key))=$([regex]::Escape($Expected))\r?$") {
    throw "$CaseName expected $Key=$Expected; got: $Output"
  }
}
function Test-Scope([string]$Name, [string[]]$Paths, [string]$ExpectedBuilds) {
  Invoke-Git @('reset','--hard',$script:base) | Out-Null
  Invoke-Git @('clean','-fdq') | Out-Null
  foreach ($relative in $Paths) {
    $full = Join-Path $temp ($relative -replace '/', [IO.Path]::DirectorySeparatorChar)
    $parent = Split-Path -Parent $full
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
    Set-Content -LiteralPath $full -Value "fixture $Name" -NoNewline
    Invoke-Git @('add','--',$relative) | Out-Null
  }
  Invoke-Git @('commit','-m',"scope test: $Name") | Out-Null
  $head = Invoke-Git @('rev-parse','HEAD')
  Push-Location $temp
  try {
    $output = & pwsh -NoProfile -File $selector -EventName pull_request -BaseRef $script:base -HeadRef $head
    if ($LASTEXITCODE -ne 0) { throw "${Name}: selector exited $LASTEXITCODE" }
  } finally { Pop-Location }
  $joined = $output -join [Environment]::NewLine
  Assert-Output $joined 'has_builds' $ExpectedBuilds $Name
  Write-Output "PASS $Name"
}

try {
  Invoke-Git @('init','-q') | Out-Null
  Invoke-Git @('config','user.name','CI scope test') | Out-Null
  Invoke-Git @('config','user.email','ci-scope@example.invalid') | Out-Null
  Set-Content -LiteralPath (Join-Path $temp 'README.md') -Value 'base' -NoNewline
  Invoke-Git @('add','README.md') | Out-Null
  Invoke-Git @('commit','-m','base') | Out-Null
  $script:base = Invoke-Git @('rev-parse','HEAD')

  Test-Scope 'packaging-only script' @('packaging/nuget/preserve_nupkg_permissions.py') 'false'
  Test-Scope 'release tag script' @('.github/scripts/release-tag.sh') 'false'
  Test-Scope 'packaging workflow' @('.github/workflows/sdk-package.yml') 'false'
  Test-Scope 'documentation-only' @('docs/example.md') 'false'
  Test-Scope 'native source' @('databind/runtime/data_bind.c') 'true'
  Test-Scope 'mixed packaging and source' @('packaging/nuget/README.md','databind/runtime/data_bind.c') 'true'
  Test-Scope 'unknown path is conservative' @('new-module/unknown.c') 'true'
  Test-Scope 'scope selector change is native' @('cmake/ci/select-ci-scope.ps1') 'true'

  Push-Location $temp
  try {
    $releaseOutput = & $selector -EventName workflow_dispatch `
      -HeadRef $script:base -PrepareRelease $true
    if ($LASTEXITCODE -ne 0) { throw "release selector exited $LASTEXITCODE" }
  } finally { Pop-Location }
  $buildLine = @($releaseOutput | Where-Object { $_ -like 'builds=*' })
  if ($buildLine.Count -ne 1) { throw 'release selector did not emit one builds value' }
  $releaseBuilds = ($buildLine[0].Substring('builds='.Length) | ConvertFrom-Json).include
  $actualRids = @($releaseBuilds.rid | Sort-Object)
  $expectedRids = @('android-arm64', 'linux-arm64', 'linux-x64',
                    'macos-arm64', 'windows-x64') | Sort-Object
  if (($actualRids -join ',') -ne ($expectedRids -join ',')) {
    throw "release RID set mismatch: $($actualRids -join ',')"
  }
  if (@($releaseBuilds | Where-Object { $_.family -eq 'ios' }).Count -ne 0) {
    throw 'release selector must not emit iOS without matching dependency SDKs'
  }
  $windows = @($releaseBuilds | Where-Object { $_.family -eq 'windows' })
  if ($windows.Count -ne 1 -or
      $windows[0].triplet -ne 'qigao-x64-windows-llvm-mingw' -or
      $windows[0].host_triplet -ne 'qigao-x64-windows-llvm-mingw') {
    throw 'Windows release must use LLVM-MinGW for target and host dependencies'
  }
  Write-Output 'PASS release RID contract'

  Push-Location $temp
  try {
    $rcOutput = & $selector -EventName push -BaseRef $script:base `
      -HeadRef $script:base -HeadBranch 'release/salts-utils-0.1-rc'
    if ($LASTEXITCODE -ne 0) { throw "RC selector exited $LASTEXITCODE" }
  } finally { Pop-Location }
  $rcBuildLine = @($rcOutput | Where-Object { $_ -like 'builds=*' })
  if ($rcBuildLine.Count -ne 1) { throw 'RC selector did not emit one builds value' }
  $rcBuilds = ($rcBuildLine[0].Substring('builds='.Length) | ConvertFrom-Json).include
  if (@($rcBuilds).Count -ne 5 -or @($rcBuilds | Where-Object { -not $_.package }).Count -ne 0) {
    throw '0.1 RC push must package all five release RIDs'
  }
  Write-Output 'PASS 0.1 RC branch contract'
} finally {
  Remove-Item -LiteralPath $temp -Recurse -Force -ErrorAction SilentlyContinue
}
