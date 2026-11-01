# SPDX-License-Identifier: MIT
# Copyright (c) 2026 MadScient

# Read-only probe of a Y8960 Cartridge through MGA.
# Never writes 01h/02h/03h to I/O 46h (Flash write/erase). Never sends break.
param(
    [Parameter(Mandatory = $true)][string]$Port,
    [Parameter(Mandatory = $true)][string]$LogPath
)
$ErrorActionPreference = 'Stop'

$dev = Get-CimInstance Win32_PnPEntity | Where-Object { $_.Name -match "\($Port\)" }
if (-not $dev -or $dev.PNPDeviceID -notmatch 'VID_0B49&PID_26D2') {
    Write-Output "REFUSE: $Port is not MGA (0b49:26d2): $($dev.PNPDeviceID)"
    exit 2
}

$log = New-Object System.Collections.Generic.List[string]
$sw = [System.Diagnostics.Stopwatch]::StartNew()
function Log([string]$s) { $line = ('{0,8:F1} ms  {1}' -f $sw.Elapsed.TotalMilliseconds, $s); $log.Add($line); Write-Host $line }

$sp = New-Object System.IO.Ports.SerialPort $Port, 115200, 'None', 8, 'One'
$sp.DtrEnable = $true
$sp.RtsEnable = $true
$sp.NewLine = "`n"
$sp.ReadTimeout = 3000
$sp.Encoding = [System.Text.Encoding]::ASCII
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

$terminators = '^(OK|FAIL|Bad Parameter|Unknow Error|Unknown command:.*)$'
function Cmd([string]$c, [switch]$Quiet) {
    $sp.Write("$c`r`n")
    $lines = @()
    while ($true) {
        $l = $sp.ReadLine().TrimEnd("`r")
        $lines += $l
        if ($l -match $terminators) { break }
    }
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
function ScExec([int]$c) {
    IoWr 0x46 $c
    $n = 0
    while ($true) { $n++; $e = IoRd 0x40; if ($e -eq 0x7F) { return $n }; if ($n -ge 1000) { throw "sc_exec timeout cmd=$c last=$e" } }
}
function RomRead([int]$addr, [int]$len) {
    $bytes = New-Object byte[] $len
    $maxPoll = 0
    for ($i = 0; $i -lt $len; $i++) {
        $a = $addr + $i
        IoWr 0x43 ($a -band 0xFF); IoWr 0x44 (($a -shr 8) -band 0xFF); IoWr 0x45 (($a -shr 16) -band 0xFF)
        $p = ScExec 0x00
        if ($p -gt $maxPoll) { $maxPoll = $p }
        $bytes[$i] = [byte](IoRd 0x47)
    }
    Log (('ROM {0:X6}h x{1}: {2}  (max polls of 40h: {3})' -f $addr, $len, (($bytes | ForEach-Object { $_.ToString('X2') }) -join ' '), $maxPoll))
    return ,$bytes
}

$powered = $false
try {
    Log ("drained before resync: {0} bytes" -f (Drain))
    $null = Cmd 'HVER'
    Log ("drained after HVER: {0} bytes" -f (Drain))
    $null = Cmd 'HVER'
    $null = Cmd 'HINF'
    $schk = Cmd 'SCHK'
    if ($schk[0] -ne '0010') { throw "STOP: no cartridge in SLOT1" }

    $powered = $true
    $r = Cmd 'SPON'
    if ($r[-1] -ne 'OK') { throw "STOP: SPON failed" }
    $t0 = $sw.Elapsed.TotalMilliseconds
    Log 'SPON done; polling enabler (write 80h to 40h, read 40h) every ~50 ms'

    $prev = -1; $okRun = 0
    while ($true) {
        IoWr 0x40 0x80
        $e = IoRd 0x40
        $dt = $sw.Elapsed.TotalMilliseconds - $t0
        if ($e -ne $prev) { Log ('  +{0:F0} ms after SPON: 40h = {1:X2}h' -f $dt, $e); $prev = $e }
        if ($e -eq 0x7F) { $okRun++ } else { $okRun = 0 }
        if ($okRun -ge 20) { break }
        if ($dt -gt 15000) { throw 'STOP: enabler never settled to 7Fh within 15 s' }
        Start-Sleep -Milliseconds 50
    }

    IoWr 0x41 0x02; $d = IoRd 0x41
    IoWr 0x42 0x01; $f = IoRd 0x42
    Log ('detect: 41h = {0:X2}h (expect FDh), 42h = {1:X2}h (expect FEh)' -f $d, $f)
    if ($d -ne 0xFD -or $f -ne 0xFE) { throw 'STOP: not detected' }

    $p = ScExec 0x04
    Log ('status: SR1 = {0:X2}h (polls of 40h: {1})' -f (IoRd 0x47), $p)

    $v = RomRead 0x7DFF00 32
    $cfg = [System.Text.Encoding]::ASCII.GetString($v, 0, 16).Split([char]0)[0]
    $bio = [System.Text.Encoding]::ASCII.GetString($v, 16, 16).Split([char]0)[0]
    Log "Cfg. Ver: $cfg"
    Log "BIOS Ver: $bio"
    $null = RomRead 0x7E0000 16
    $null = RomRead 0x000000 48

    $t1 = $sw.Elapsed.TotalMilliseconds
    $null = RomRead 0x7E0010 64
    Log ('64 byte read took {0:F0} ms' -f ($sw.Elapsed.TotalMilliseconds - $t1))
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
