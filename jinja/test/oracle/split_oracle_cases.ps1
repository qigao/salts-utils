param(
  [string]$CasesFile = "cases.json",
  [string]$OutputDir = "cases"
)

$input_path = Join-Path $PSScriptRoot $CasesFile
$output_dir = Join-Path $PSScriptRoot $OutputDir

if (-not (Test-Path -LiteralPath $input_path)) {
  throw "missing case file: $input_path"
}

$cases = Get-Content -Raw -LiteralPath $input_path | ConvertFrom-Json

$groups = [ordered]@{}
foreach ($case in $cases) {
  $name = $case.name
  if ($null -eq $name -or $name -eq "") {
    throw "each oracle case requires a non-empty name"
  }
  if ($name -match '^[^_]+') {
    $key = $matches[0]
  } else {
    $key = 'misc'
  }
  if (-not $groups.Contains($key)) {
    $groups[$key] = [System.Collections.Generic.List[object]]::new()
  }
  $groups[$key].Add($case) | Out-Null
}

New-Item -ItemType Directory -Force -Path $output_dir | Out-Null
Get-ChildItem -Path $output_dir -Filter *.json | Remove-Item -Force

foreach ($entry in $groups.GetEnumerator()) {
  $target = Join-Path $output_dir ("{0}.json" -f $entry.Key)
  ,@($entry.Value) | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $target -Encoding UTF8
}
