param(
  [Parameter(Mandatory=$true)][string]$RunId,
  [Parameter(Mandatory=$true)][string]$Commit,
  [Parameter(Mandatory=$true)][string]$Rid
)
$ErrorActionPreference = 'Stop'
if ($RunId -notmatch '^[1-9][0-9]*$' -or $Commit -cnotmatch '^[0-9a-f]{40}$') {
  throw 'CNet once candidate requires an exact Actions run and immutable source SHA'
}
$expectedRids = @('linux-x64', 'linux-arm64', 'windows-x64', 'macos-arm64',
                  'android-arm64-v8a', 'ios-arm64')
if ($Rid -cnotin $expectedRids) {
  throw "Unexpected candidate RID $Rid"
}
if ([string]::IsNullOrWhiteSpace($env:GH_TOKEN) -or
    [string]::IsNullOrWhiteSpace($env:GITHUB_ENV)) {
  throw 'Missing authorized Actions artifact token / environment'
}
$runJson = gh api "repos/qigao/salts/actions/runs/$RunId"
if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect exact Salts candidate producer' }
$run = $runJson | ConvertFrom-Json
if ($run.repository.full_name -cne 'qigao/salts' -or
    $run.path -cne '.github/workflows/cnet-once-six-rid-candidate.yml' -or
    $run.event -cne 'pull_request' -or
    $run.head_branch -cne 'test/cnet-module-once-tsan-1143' -or
    $run.head_sha -cne $Commit -or
    $run.status -cne 'completed' -or $run.conclusion -cne 'success') {
  throw 'Wrong candidate origin, exact source or incomplete six-RID qualification'
}
$matchingPRs = @($run.pull_requests | Where-Object {
  $_.number -eq 1150 -and $_.head.sha -eq $Commit
})
if ($matchingPRs.Count -ne 1) {
  throw 'Candidate is not the expected Salts Draft PR #1150 head'
}
$artifact = if ($Rid -eq 'macos-arm64') { 'salts-sdk-macos' }
            else { "salts-sdk-$Rid" }
$destination = Join-Path $env:RUNNER_TEMP "salts-cnet-once-pr-$RunId-$Rid"
if (Test-Path -LiteralPath $destination) {
  throw "Refusing existing SDK restoration directory $destination"
}
gh run download $RunId --repo qigao/salts --name $artifact --dir $destination
if ($LASTEXITCODE -ne 0) { throw "Unable to download $artifact from $RunId" }
$root = Join-Path $destination $Rid
$manifestPath = Join-Path $root 'salts-sdk-manifest.txt'
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
  throw "Missing exact candidate SDK manifest: $manifestPath"
}
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-StringData
$expectedVersion = "2.3.0-cnet.once.sha$Commit"
if ($manifest.commit -cne $Commit -or
    $manifest.version -cne $expectedVersion -or
    $manifest.profile -cne 'release') {
  throw "Candidate manifest mismatch at $Rid"
}
$required = @(
  (Join-Path $root 'lib/cmake/Salts/SaltsConfig.cmake'),
  (Join-Path $root 'include/salts/thread.h'),
  (Join-Path $root 'include/cmeta/function.h')
)
foreach ($item in $required) {
  if (-not (Test-Path -LiteralPath $item -PathType Leaf)) {
    throw "Incomplete candidate SDK $Rid : $item"
  }
}
$header = Get-Content -LiteralPath (Join-Path $root 'include/salts/thread.h') -Raw
if ($Rid -ne 'windows-x64' -and $header -cnotmatch 'pthread_once_t native;') {
  throw "Candidate POSIX once ABI mismatch for $Rid"
}
"SALTS_CANDIDATE_ROOT=$root" >> $env:GITHUB_ENV
Write-Host "Selected exact candidate SDK $Rid, source $Commit, run $RunId"
