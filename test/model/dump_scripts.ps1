# SPDX-License-Identifier: MIT
# Copyright (c) 2026 MadScient

# Writes the scripts that test/hw/bios_block.ps1 would send, for test/model/sim_mga.py.
param([string]$Src, [string]$OutDir, [string]$Data)
$ast = [System.Management.Automation.Language.Parser]::ParseFile($Src, [ref]$null, [ref]$null)
$want = 'Add-Instr', 'Read-Rom', 'Write-Rom'
$ast.FindAll({ $args[0] -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $args[0].Name -in $want }, $true) |
    ForEach-Object { . ([scriptblock]::Create($_.Extent.Text)) }
$ResultBase = 0xC000
$ScriptLimit = 0xBF00
$StatusWaitUs = 20
$script:n = 0
function Run-Script([byte[]]$s) { [System.IO.File]::WriteAllBytes(('{0}\s{1:D3}.bin' -f $OutDir, $script:n), $s); $script:n++ }
function Receive-Buffer([int]$addr, [int]$len) { return ,(New-Object byte[] $len) }
function Log([string]$s) { Write-Host $s }

$srcBytes = [System.IO.File]::ReadAllBytes($Data)
$block = New-Object byte[] 0x10000
[Array]::Copy($srcBytes, 0, $block, 0, 0x10000)
Write-Rom 0x7E0000 $block
$w = $script:n
$null = Read-Rom 0x7E0000 0x10000
Write-Host "write scripts: $w, total scripts: $($script:n)"
try { $l = New-Object System.Collections.Generic.List[byte]; $script:scrAddrH = 0x7D; Add-Instr $l 0x04 0x46 0x01; Write-Host 'GUARD FAILED (H=7D)' } catch { Write-Host "guard H=7D: $($_.Exception.Message)" }
try { $l = New-Object System.Collections.Generic.List[byte]; $script:scrAddrH = 0x7E; Add-Instr $l 0x04 0x46 0x02; Write-Host 'GUARD FAILED (erase in script)' } catch { Write-Host "guard erase: $($_.Exception.Message)" }
