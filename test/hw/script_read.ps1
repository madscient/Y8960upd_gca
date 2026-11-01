# SPDX-License-Identifier: MIT
# Copyright (c) 2026 MadScient

# Read-only measurement of MGA script mode reading the Y8960 ConfigROM.
# The generated scripts write only I/O 40h-46h, and 46h only with 00h (read).
param(
    [Parameter(Mandatory = $true)][string]$Port,
    [Parameter(Mandatory = $true)][string]$LogPath,
    [Parameter(Mandatory = $true)][string]$Expect,
    [int]$RomAddr = 0x7E0000,
    [int]$Count = 1024
)
$ErrorActionPreference = 'Stop'

$dev = Get-CimInstance Win32_PnPEntity | Where-Object { $_.Name -match "\($Port\)" }
if (-not $dev -or $dev.PNPDeviceID -notmatch 'VID_0B49&PID_26D2') {
    Write-Output "REFUSE: $Port is not MGA (0b49:26d2): $($dev.PNPDeviceID)"
    exit 2
}

$expected = [System.IO.File]::ReadAllBytes($Expect)
$expOffset = $RomAddr - 0x7E0000

$log = New-Object System.Collections.Generic.List[string]
$sw = [System.Diagnostics.Stopwatch]::StartNew()
function Log([string]$s) { $line = ('{0,8:F1} ms  {1}' -f $sw.Elapsed.TotalMilliseconds, $s); $log.Add($line); Write-Host $line }

$sp = New-Object System.IO.Ports.SerialPort $Port, 115200, 'None', 8, 'One'
$sp.DtrEnable = $true
$sp.RtsEnable = $true
$sp.ReadTimeout = 5000
$sp.WriteTimeout = 5000
$sp.Open()

function Drain {
    $n = 0
    $quiet = [System.Diagnostics.Stopwatch]::StartNew()
    while ($quiet.ElapsedMilliseconds -lt 300) {
        if ($sp.BytesToRead -gt 0) { $n += $sp.ReadExisting().Length; $quiet.Restart() }
        else { Start-Sleep -Milliseconds 10 }
    }
    return $n
}
function ReadLineAscii {
    $sb = New-Object System.Text.StringBuilder
    while ($true) {
        $b = $sp.ReadByte()
        if ($b -eq 10) { break }
        if ($b -ne 13) { [void]$sb.Append([char]$b) }
    }
    return $sb.ToString()
}
$terminators = '^(OK|FAIL|Bad Parameter|Unknow Error|Unknown command:.*)$'
function ReadResponse {
    $lines = @()
    while ($true) { $l = ReadLineAscii; $lines += $l; if ($l -match $terminators) { break } }
    return ,$lines
}
function Cmd([string]$c, [switch]$Quiet) {
    $sp.Write("$c`r`n")
    $lines = ReadResponse
    if (-not $Quiet) { Log ("> $c  < " + ($lines -join ' | ')) }
    return ,$lines
}
function IoRd([int]$io) {
    $r = Cmd ('IORD,{0:X}' -f $io) -Quiet
    if ($r[-1] -ne 'OK') { throw "IORD $io failed: $($r -join ' | ')" }
    $m = [regex]::Match($r[0], '^[0-9a-fA-F]{4} : ([0-9a-fA-F]{2})$')
    if (-not $m.Success) { throw "IORD $io unexpected: $($r -join ' | ')" }
    return [Convert]::ToInt32($m.Groups[1].Value, 16)
}
function IoWr([int]$io, [int]$v) {
    if ($io -eq 0x46 -and $v -ne 0x00 -and $v -ne 0x04) { throw "refused: command $v" }
    $r = Cmd ('IOWR,{0:X},{1:X}' -f $io, $v) -Quiet
    if ($r[-1] -ne 'OK') { throw "IOWR $io,$v failed: $($r -join ' | ')" }
}

function Add-Instr($list, [int]$op, [int]$addr, [int]$data) {
    if ($op -eq 0x04) {
        if ($addr -lt 0x40 -or $addr -gt 0x46) { throw "refused: script write to I/O $addr" }
        if ($addr -eq 0x46 -and $data -ne 0x00) { throw "refused: script command $data" }
    }
    $list.Add([byte]$op); $list.Add([byte](($addr -shr 8) -band 0xFF)); $list.Add([byte]($addr -band 0xFF)); $list.Add([byte]$data)
}
# Script and results share the 64KB buffer: script from 0000h, results from C000h.
$ResultBase = 0xC000
function Build-ReadScript([int]$addr, [int]$count, [bool]$fullAddress) {
    $s = New-Object System.Collections.Generic.List[byte]
    $prevM = -1; $prevH = -1
    for ($i = 0; $i -lt $count; $i++) {
        $a = $addr + $i
        $l = $a -band 0xFF; $m = ($a -shr 8) -band 0xFF; $h = ($a -shr 16) -band 0xFF
        Add-Instr $s 0x04 0x43 $l
        if ($fullAddress -or $m -ne $prevM) { Add-Instr $s 0x04 0x44 $m; $prevM = $m }
        if ($fullAddress -or $h -ne $prevH) { Add-Instr $s 0x04 0x45 $h; $prevH = $h }
        Add-Instr $s 0x04 0x46 0x00
        Add-Instr $s 0x03 0x40 0x00
        Add-Instr $s 0x06 0x0000 0x7F
        Add-Instr $s 0x0A 0x0000 0xFE
        Add-Instr $s 0x03 0x47 0x00
        Add-Instr $s 0x0B ($ResultBase + $i) 0x00
    }
    Add-Instr $s 0xFF 0x0000 0x00
    return ,($s.ToArray())
}

function Send-Buffer([byte[]]$data) {
    if ($data.Length -ge $ResultBase) { throw "script too long: $($data.Length)" }
    $head = [System.Text.Encoding]::ASCII.GetBytes(('BRCV,0,{0:X}' -f $data.Length) + "`r`n")
    $t = $sw.Elapsed.TotalMilliseconds
    $sp.Write($head, 0, $head.Length)
    $sp.Write($data, 0, $data.Length)
    $r = ReadResponse
    Log ('BRCV {0} bytes: {1} ({2:F0} ms)' -f $data.Length, ($r -join ' | '), ($sw.Elapsed.TotalMilliseconds - $t))
    if ($r[-1] -ne 'OK') { throw 'BRCV failed' }
}
function Receive-Buffer([int]$addr, [int]$len) {
    $t = $sw.Elapsed.TotalMilliseconds
    $sp.Write(('BSND,{0:X},{1:X}' -f $addr, $len) + "`r`n")
    $buf = New-Object byte[] $len
    $got = 0
    while ($got -lt $len) { $got += $sp.Read($buf, $got, $len - $got) }
    $r = ReadResponse
    Log ('BSND {0} bytes: {1} ({2:F0} ms)' -f $len, ($r -join ' | '), ($sw.Elapsed.TotalMilliseconds - $t))
    if ($r[-1] -ne 'OK') { throw 'BSND failed' }
    return ,$buf
}

$powered = $false
try {
    Log ("drained before resync: {0} bytes" -f (Drain))
    $null = Cmd 'HVER'
    Log ("drained after HVER: {0} bytes" -f (Drain))
    $schk = Cmd 'SCHK'
    if ($schk[0] -ne '0010') { throw 'STOP: no cartridge in SLOT1' }

    $powered = $true
    $r = Cmd 'SPON'
    if ($r[-1] -ne 'OK') { throw 'STOP: SPON failed' }
    $t0 = $sw.Elapsed.TotalMilliseconds
    $okRun = 0
    while ($okRun -lt 20) {
        IoWr 0x40 0x80
        if ((IoRd 0x40) -eq 0x7F) { $okRun++ } else { $okRun = 0 }
        if ($sw.Elapsed.TotalMilliseconds - $t0 -gt 15000) { throw 'STOP: enabler never settled to 7Fh within 15 s' }
        Start-Sleep -Milliseconds 50
    }
    IoWr 0x41 0x02; $d = IoRd 0x41
    IoWr 0x42 0x01; $f = IoRd 0x42
    Log ('detect: 41h = {0:X2}h, 42h = {1:X2}h' -f $d, $f)
    if ($d -ne 0xFD -or $f -ne 0xFE) { throw 'STOP: not detected' }

    foreach ($full in @($true, $false)) {
        $name = if ($full) { 'A (all 3 address bytes)' } else { 'B (changed address bytes only)' }
        Log "--- variant $name, ${Count} bytes from $('{0:X6}' -f $RomAddr)h"
        $script = Build-ReadScript $RomAddr $Count $full
        Send-Buffer $script
        $t = $sw.Elapsed.TotalMilliseconds
        $r = Cmd 'BSCR,0'
        Log ('BSCR took {0:F0} ms' -f ($sw.Elapsed.TotalMilliseconds - $t))
        if ($r[-1] -ne 'OK') { throw 'BSCR failed' }
        $got = Receive-Buffer $ResultBase $Count
        [System.IO.File]::WriteAllBytes(("{0}.{1}.bin" -f $LogPath, $(if ($full) { 'A' } else { 'B' })), $got)
        $bad = 0; $first = -1
        for ($i = 0; $i -lt $Count; $i++) { if ($got[$i] -ne $expected[$expOffset + $i]) { $bad++; if ($first -lt 0) { $first = $i } } }
        Log ('compare with expect file: {0} mismatches{1}' -f $bad, $(if ($first -ge 0) { (' (first at +{0:X}: got {1:X2}h, expect {2:X2}h)' -f $first, $got[$first], $expected[$expOffset + $first]) } else { '' }))
    }
}
catch {
    Log "ERROR: $($_.Exception.Message)"
}
finally {
    try {
        if ($powered) { IoWr 0x40 0x00; $null = Cmd 'SPOFF' }
    } catch { Log "ERROR in cleanup: $($_.Exception.Message)" }
    $sp.Close()
    $log | Set-Content -Path $LogPath -Encoding UTF8
}
