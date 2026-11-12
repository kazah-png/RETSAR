#requires -Version 5.1
# Parse source and inspect marker expressions only. Never execute the guest
# reference script, its native declarations, account commands or credentials.
$ErrorActionPreference = 'Stop'
$base = Split-Path -Parent $PSScriptRoot
$referencePath = Join-Path $base 'lab\guest-rdp-reference.ps1'
$tokens = $null
$parseErrors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile(
    $referencePath, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count) { throw ('Reference syntax failed: ' + ($parseErrors.Message -join '; ')) }

$checks = [Collections.Generic.List[string]]::new()
function Check([bool]$Condition, [string]$Name) {
    if (!$Condition) { throw ('FAIL RDP REFERENCE: ' + $Name) }
    $checks.Add($Name)
    Write-Output ('PASS RDP REFERENCE: ' + $Name)
}
$assignments = @($ast.FindAll({
    param($node)
    $node -is [Management.Automation.Language.AssignmentStatementAst] -and
        $node.Left -is [Management.Automation.Language.VariableExpressionAst] -and
        $node.Left.VariablePath.UserPath -ceq 'marker'
}, $true))
Check ($assignments.Count -eq 2) 'Exactly two marker assignments remain'

$prefixes = @{}
foreach ($assignment in $assignments) {
    $expressions = @($assignment.Right.FindAll({
        param($node)
        $node -is [Management.Automation.Language.BinaryExpressionAst]
    }, $true))
    if ($expressions.Count -ne 1) { throw 'Marker must be a single literal-plus-runId expression.' }
    $expression = $expressions[0]
    if ($expression.Operator -ne [Management.Automation.Language.TokenKind]::Plus -or
        $expression.Left -isnot [Management.Automation.Language.StringConstantExpressionAst]) {
        throw 'Marker prefix must remain a literal concatenated with the complete run ID.'
    }
    $operand = $expression.Right
    if ($operand -is [Management.Automation.Language.VariableExpressionAst] -and
        $operand.VariablePath.UserPath -ceq 'runId') {
        $branch = 'Connect'
    } elseif ($operand -is [Management.Automation.Language.MemberExpressionAst] -and
        $operand.Expression -is [Management.Automation.Language.VariableExpressionAst] -and
        $operand.Expression.VariablePath.UserPath -ceq 'receipt' -and
        $operand.Member -is [Management.Automation.Language.StringConstantExpressionAst] -and
        $operand.Member.Value -ceq 'runId' -and !$operand.Static) {
        $branch = 'Cleanup'
    } else {
        throw 'Marker must append runId directly, without truncation or normalization.'
    }
    Check (!$prefixes.ContainsKey($branch)) ($branch + ' appends the complete runId')
    $prefixes[$branch] = $expression.Left.Value
}
Check ($prefixes.Count -eq 2 -and $prefixes.Connect -ceq $prefixes.Cleanup) (
    'Connect and Cleanup derive the same marker prefix')

$runIds = @(
    '0123456789abcdef0123456789abcdef',
    'FEDCBA9876543210FEDCBA9876543210',
    '00000000000000000000000000000000'
)
# New-LocalUser documents a 48-character Description limit:
# https://learn.microsoft.com/en-us/powershell/module/microsoft.powershell.localaccounts/new-localuser?view=powershell-5.1#-description
$markers = @()
foreach ($runId in $runIds) {
    $parsed = [guid]::Empty
    if (![guid]::TryParseExact($runId, 'N', [ref]$parsed)) { throw 'Invalid synthetic GUID fixture.' }
    # The inspected AST permits only string addition; no source is evaluated.
    $connectMarker = $prefixes.Connect + $runId
    $cleanupMarker = $prefixes.Cleanup + $runId
    Check ($connectMarker -ceq $cleanupMarker -and $connectMarker.Length -eq 43 -and
        $connectMarker.Length -le 48 -and
        $connectMarker.StartsWith('RETSAR ref ', [StringComparison]::Ordinal) -and
        $connectMarker.Substring(11) -ceq $runId) ('Description fits 48 characters with complete GUID: ' + $runId)
    $markers += $connectMarker
}
Check (@($markers | Select-Object -Unique).Count -eq $runIds.Count) 'Different full GUIDs retain distinct markers'

$createCommands = @($ast.FindAll({
    param($node)
    $node -is [Management.Automation.Language.CommandAst] -and
        $node.GetCommandName() -ceq 'New-LocalUser'
}, $true))
if ($createCommands.Count -ne 1) { throw 'Expected exactly one account creation command in source.' }
$descriptionUsesMarker = $false
$elements = $createCommands[0].CommandElements
for ($i = 0; $i -lt $elements.Count - 1; $i++) {
    if ($elements[$i] -is [Management.Automation.Language.CommandParameterAst] -and
        $elements[$i].ParameterName -ceq 'Description') {
        $value = $elements[$i + 1]
        $descriptionUsesMarker = $value -is [Management.Automation.Language.VariableExpressionAst] -and
            $value.VariablePath.UserPath -ceq 'marker'
    }
}
Check $descriptionUsesMarker 'Account description consumes the checked marker'
Write-Output ('RDP REFERENCE TOTAL ' + $checks.Count + ' passed, 0 failed; AST and string checks only, no account or credential operations.')
