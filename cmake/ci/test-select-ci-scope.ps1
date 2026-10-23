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
  if ($Output -notmatch "(?m)^$([regex]::Escape($Key))=$([regex]::Escape($Expected))$") {
    throw "$CaseName expected $Key=$Expected; got: $Output"
  }
}
function Test-Scope([string]$Name, [string[]]$Paths, [string]$ExpectedPackaging, [string]$ExpectedBuilds) {
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
  Assert-Output $joined 'packaging_only' $ExpectedPackaging $Name
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

  Test-Scope 'packaging-only script' @('packaging/nuget/preserve_nupkg_permissions.py') 'true' 'false'
  Test-Scope 'packaging workflow' @('.github/workflows/sdk-package.yml') 'true' 'false'
  Test-Scope 'documentation-only' @('docs/example.md') 'false' 'false'
  Test-Scope 'native source' @('databind/runtime/data_bind.c') 'false' 'true'
  Test-Scope 'mixed packaging and source' @('packaging/nuget/README.md','databind/runtime/data_bind.c') 'false' 'true'
  Test-Scope 'unknown path is conservative' @('new-module/unknown.c') 'false' 'true'
  Test-Scope 'scope selector change is native' @('cmake/ci/select-ci-scope.ps1') 'false' 'true'
} finally {
  Remove-Item -LiteralPath $temp -Recurse -Force -ErrorAction SilentlyContinue
}
