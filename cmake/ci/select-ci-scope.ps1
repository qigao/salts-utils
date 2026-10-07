param(
  [Parameter(Mandatory)][ValidateSet('pull_request', 'push', 'workflow_dispatch')][string]$EventName,
  [string]$BaseRef,
  [Parameter(Mandatory)][string]$HeadRef,
  [bool]$PrepareRelease = $false
)
$ErrorActionPreference = 'Stop'
if ($PrepareRelease -and $EventName -ne 'workflow_dispatch') {
  throw 'Release preparation requires a manual CI run'
}
$full = $EventName -eq 'workflow_dispatch' -or $BaseRef -match '^0+$'
$changed = @()
if (-not $full) {
  if ([string]::IsNullOrWhiteSpace($BaseRef)) { throw 'A change base is required' }
  $changed = @(git diff --name-only $BaseRef $HeadRef --)
  if ($LASTEXITCODE -ne 0) { throw 'Could not determine changed files' }
}
# Unknown paths select the full graph. Only documentation-only changes skip it;
# adding a module must never require extending a CI source/target allowlist.
$native = $full -or @($changed | Where-Object {
  $_ -notmatch '^(docs/|.*\.md$|LICENSE$|NOTICE$|\.gitignore$|\.editorconfig$)'
}).Count -gt 0
$profiles = @(
  @{ id='linux-release'; runner='ubuntu-24.04'; family='linux'; rid='linux-x64'; host='linux-x64'; triplet='x64-linux'; host_triplet='x64-linux'; preset='ci-native-release-user' },
  @{ id='windows-release'; runner='windows-2025'; family='windows'; rid='windows-x64'; host='windows-x64'; triplet=''; host_triplet=''; preset='ci-win-release-user' },
  @{ id='macos-release'; runner='macos-15'; family='mac'; rid='macos-arm64'; host='macos-arm64'; triplet='arm64-osx'; host_triplet='arm64-osx'; preset='ci-macos-release-user' },
  @{ id='linux-sanitizers'; runner='ubuntu-24.04'; family='linux'; rid='linux-x64'; host='linux-x64'; triplet='x64-linux'; host_triplet='x64-linux'; preset='ci-linux-dev-user' },
  @{ id='android-release'; runner='ubuntu-24.04'; family='android'; rid='android-arm64-v8a'; host='linux-x64'; triplet='arm64-android'; host_triplet='x64-linux'; preset='ci-android-sdk-release-user' },
  @{ id='ios-release'; runner='macos-15'; family='ios'; rid='ios-arm64'; host='macos-arm64'; triplet='arm64-ios'; host_triplet='arm64-osx'; sysroot='iphoneos'; preset='ci-ios-sdk-release-user' },
  @{ id='ios-simulator-release'; runner='macos-15'; family='ios'; rid='ios-simulator-arm64'; host='macos-arm64'; triplet='arm64-ios-simulator'; host_triplet='arm64-osx'; sysroot='iphonesimulator'; preset='ci-ios-sdk-release-user' }
)
if ($PrepareRelease) {
  $profiles += @{ id='linux-arm64-release'; runner='ubuntu-24.04-arm'; family='linux'; rid='linux-arm64'; host='linux-arm64'; triplet='arm64-linux'; host_triplet='arm64-linux'; preset='ci-native-minimal-release-user' }
}
$builds = @()
$tests = @()
if ($native) {
  foreach ($profile in $profiles) {
    $profile.cross = $profile.family -in @('android', 'ios')
    $profile.package = $PrepareRelease -and $profile.id -ne 'linux-sanitizers'
    $profile.build_dir = switch ($profile.id) {
      'windows-release' { 'build/ci-win-release' }
      'linux-sanitizers' { 'build/ci-linux-dev' }
      default { "build/ci-sdk/$($profile.rid)" }
    }
    if (-not $profile.ContainsKey('sysroot')) { $profile.sysroot = '' }
    $builds += $profile
    if (-not $profile.cross) { $tests += $profile }
  }
}
$outputs = [ordered]@{
  builds = ConvertTo-Json -Depth 5 -Compress -InputObject @{ include = @($builds) }
  tests = ConvertTo-Json -Depth 5 -Compress -InputObject @{ include = @($tests) }
  has_builds = ($builds.Count -gt 0).ToString().ToLowerInvariant()
  has_tests = ($tests.Count -gt 0).ToString().ToLowerInvariant()
}
foreach ($key in $outputs.Keys) {
  Write-Output "$key=$($outputs[$key])"
  if ($env:GITHUB_OUTPUT) { Add-Content -LiteralPath $env:GITHUB_OUTPUT -Value "$key=$($outputs[$key])" }
}
