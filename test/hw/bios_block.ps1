# SPDX-License-Identifier: MIT
# Copyright (c) 2026 MadScient

# Erase the first 64KB block of the BIOS area (7E0000h) and write it back, through MGA.
# Only the 7E0000h block is ever erased or written. Config (below 7D0000h) is never touched.
param(
    [Parameter(Mandatory = $true)][string]$Port,
    [Parameter(Mandatory = $true)][string]$LogPath,
    # Expected current content of the block (headerless BIOS body). Erase is refused unless the
    # backup read matches it exactly.
    [string]$Expect,
    # Skip the backup and write this file's first 64KB instead (recovery after a failed run).
    [string]$RestoreFrom
)
$ErrorActionPreference = 'Stop'
$Block = 0x7E0000
$BlockSize = 0x10000

$dev = Get-CimInstance Win32_PnPEntity | Where-Object { $_.Name -match "\($Port\)" }
if (-not $dev -or $dev.PNPDeviceID -notmatch 'VID_0B49&PID_26D2') {
    Write-Output "REFUSE: $Port is not MGA (0b49:26d2): $($dev.PNPDeviceID)"
    exit 2
}
if ([string]::IsNullOrEmpty($Expect) -eq [string]::IsNullOrEmpty($RestoreFrom)) {
    Write-Output 'REFUSE: give exactly one of -Expect or -RestoreFrom'
    exit 2
}

$log = New-Object System.Collections.Generic.List[string]
$sw = [System.Diagnostics.Stopwatch]::StartNew()
function Log([string]$s) { $line = ('{0,9:F1} ms  {1}' -f $sw.Elapsed.TotalMilliseconds, $s); $log.Add($line); Write-Host $line }

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

$script:pcAddrH = -1
$script:eraseLeft = 1
function IoRd([int]$io) {
    $r = Cmd ('IORD,{0:X}' -f $io) -Quiet
    if ($r[-1] -ne 'OK') { throw "IORD $io failed: $($r -join ' | ')" }
    $m = [regex]::Match($r[0], '^[0-9a-fA-F]{4} : ([0-9a-fA-F]{2})$')
    if (-not $m.Success) { throw "IORD $io unexpected: $($r -join ' | ')" }
    return [Convert]::ToInt32($m.Groups[1].Value, 16)
}
function IoWr([int]$io, [int]$v) {
    if ($io -eq 0x45) { $script:pcAddrH = $v }
    if ($io -eq 0x46) {
        if ($v -eq 0x02) {
            if ($script:pcAddrH -ne 0x7E -or $script:eraseLeft -le 0) { throw "refused: erase with H=$($script:pcAddrH)" }
            $script:eraseLeft--
        } elseif ($v -ne 0x00 -and $v -ne 0x04) { throw "refused: PC command $v" }
    }
    $r = Cmd ('IOWR,{0:X},{1:X}' -f $io, $v) -Quiet
    if ($r[-1] -ne 'OK') { throw "IOWR $io,$v failed: $($r -join ' | ')" }
}

# Script builder. Tracks 45h inside the script so a write command is only emitted for 7Exxxxh.
$script:scrAddrH = -1
function Add-Instr($list, [int]$op, [int]$addr, [int]$data) {
    if ($op -eq 0x04) {
        if ($addr -lt 0x40 -or $addr -gt 0x47) { throw "refused: script write to I/O $addr" }
        if ($addr -eq 0x45) { $script:scrAddrH = $data }
        if ($addr -eq 0x46) {
            if ($data -eq 0x01) { if ($script:scrAddrH -ne 0x7E) { throw "refused: script write with H=$($script:scrAddrH)" } }
            elseif ($data -ne 0x00 -and $data -ne 0x04) { throw "refused: script command $data" }
        }
    }
    $list.Add([byte]$op); $list.Add([byte](($addr -shr 8) -band 0xFF)); $list.Add([byte]($addr -band 0xFF)); $list.Add([byte]$data)
}
$ResultBase = 0xC000
$ScriptLimit = 0xBF00
$StatusWaitUs = 20

function Send-Buffer([byte[]]$data) {
    if ($data.Length -ge 0xFFFF) { throw "buffer too long: $($data.Length)" }
    $head = [System.Text.Encoding]::ASCII.GetBytes(('BRCV,0,{0:X}' -f $data.Length) + "`r`n")
    $sp.Write($head, 0, $head.Length)
    $sp.Write($data, 0, $data.Length)
    $r = ReadResponse
    if ($r[-1] -ne 'OK') { throw "BRCV failed: $($r -join ' | ')" }
}
function Receive-Buffer([int]$addr, [int]$len) {
    $sp.Write(('BSND,{0:X},{1:X}' -f $addr, $len) + "`r`n")
    $buf = New-Object byte[] $len
    $got = 0
    while ($got -lt $len) { $got += $sp.Read($buf, $got, $len - $got) }
    $r = ReadResponse
    if ($r[-1] -ne 'OK') { throw "BSND failed: $($r -join ' | ')" }
    return ,$buf
}
function Run-Script([byte[]]$s) {
    Send-Buffer $s
    $r = Cmd 'BSCR,0' -Quiet
    if ($r[-1] -ne 'OK') { throw "BSCR failed: $($r -join ' | ')" }
}

function Read-Rom([int]$addr, [int]$len) {
    $out = New-Object byte[] $len
    $chunk = 1024
    for ($off = 0; $off -lt $len; $off += $chunk) {
        $n = [Math]::Min($chunk, $len - $off)
        $s = New-Object System.Collections.Generic.List[byte]
        $script:scrAddrH = -1
        $prevM = -1; $prevH = -1
        for ($i = 0; $i -lt $n; $i++) {
            $a = $addr + $off + $i
            $l = $a -band 0xFF; $m = ($a -shr 8) -band 0xFF; $h = ($a -shr 16) -band 0xFF
            Add-Instr $s 0x04 0x43 $l
            if ($m -ne $prevM) { Add-Instr $s 0x04 0x44 $m; $prevM = $m }
            if ($h -ne $prevH) { Add-Instr $s 0x04 0x45 $h; $prevH = $h }
            Add-Instr $s 0x04 0x46 0x00
            Add-Instr $s 0x03 0x40 0x00
            Add-Instr $s 0x06 0x0000 0x7F
            Add-Instr $s 0x0A 0x0000 0xFE
            Add-Instr $s 0x03 0x47 0x00
            Add-Instr $s 0x0B ($ResultBase + $i) 0x00
        }
        Add-Instr $s 0xFF 0x0000 0x00
        Run-Script ($s.ToArray())
        $got = Receive-Buffer $ResultBase $n
        [Array]::Copy($got, 0, $out, $off, $n)
    }
    return ,$out
}

# Write bytes other than FFh. The BUSY loop does not poll 40h: a Compare that matches resets the
# script loop counter, which would make a stuck BUSY loop endless. A fixed wait replaces the poll.
function Write-Rom([int]$addr, [byte[]]$data) {
    $i = 0
    $written = 0
    $scripts = 0
    while ($i -lt $data.Length) {
        $s = New-Object System.Collections.Generic.List[byte]
        $script:scrAddrH = -1
        $prevM = -1; $prevH = -1
        while ($i -lt $data.Length -and $s.Count -lt $ScriptLimit) {
            $d = $data[$i]
            if ($d -ne 0xFF) {
                $a = $addr + $i
                $l = $a -band 0xFF; $m = ($a -shr 8) -band 0xFF; $h = ($a -shr 16) -band 0xFF
                Add-Instr $s 0x04 0x43 $l
                if ($m -ne $prevM) { Add-Instr $s 0x04 0x44 $m; $prevM = $m }
                if ($h -ne $prevH) { Add-Instr $s 0x04 0x45 $h; $prevH = $h }
                Add-Instr $s 0x04 0x47 $d
                Add-Instr $s 0x04 0x46 0x01
                Add-Instr $s 0x03 0x40 0x00
                Add-Instr $s 0x06 0x0000 0x7F
                Add-Instr $s 0x0A 0x0000 0xFE
                Add-Instr $s 0x04 0x46 0x04
                Add-Instr $s 0x05 $StatusWaitUs 0x00
                Add-Instr $s 0x03 0x47 0x00
                Add-Instr $s 0x07 0x0000 0x01
                Add-Instr $s 0x0A 0x0000 0xFC
                $written++
            }
            $i++
        }
        Add-Instr $s 0xFF 0x0000 0x00
        Run-Script ($s.ToArray())
        $scripts++
    }
    Log ('wrote {0} non-FF bytes with {1} scripts' -f $written, $scripts)
}

function Measure-CommandSettle {
    $s = New-Object System.Collections.Generic.List[byte]
    $script:scrAddrH = -1
    Add-Instr $s 0x04 0x43 0x00; Add-Instr $s 0x04 0x44 0x00; Add-Instr $s 0x04 0x45 0x7E
    for ($i = 0; $i -lt 256; $i++) { Add-Instr $s 0x04 0x46 0x04; Add-Instr $s 0x03 0x40 0x00; Add-Instr $s 0x0B ($ResultBase + $i) 0 }
    for ($i = 0; $i -lt 256; $i++) { Add-Instr $s 0x04 0x46 0x04; Add-Instr $s 0x03 0x47 0x00; Add-Instr $s 0x0B ($ResultBase + 0x100 + $i) 0 }
    for ($i = 0; $i -lt 256; $i++) { Add-Instr $s 0x04 0x46 0x00; Add-Instr $s 0x03 0x40 0x00; Add-Instr $s 0x0B ($ResultBase + 0x200 + $i) 0 }
    for ($i = 0; $i -lt 256; $i++) { Add-Instr $s 0x04 0x46 0x04; Add-Instr $s 0x05 $StatusWaitUs 0; Add-Instr $s 0x03 0x47 0x00; Add-Instr $s 0x0B ($ResultBase + 0x300 + $i) 0 }
    Add-Instr $s 0xFF 0x0000 0x00
    Run-Script ($s.ToArray())
    $r = Receive-Buffer $ResultBase 0x400
    $names = '40h right after status cmd', '47h right after status cmd (no poll)', '40h right after read cmd', "47h ${StatusWaitUs}us after status cmd (no poll)"
    for ($k = 0; $k -lt 4; $k++) {
        $h = @{}
        for ($i = 0; $i -lt 256; $i++) { $v = '{0:X2}' -f $r[$k * 256 + $i]; if ($h.ContainsKey($v)) { $h[$v]++ } else { $h[$v] = 1 } }
        Log ('  {0}: {1}' -f $names[$k], (($h.GetEnumerator() | Sort-Object Name | ForEach-Object { "$($_.Name)h x$($_.Value)" }) -join ', '))
    }
}

function Erase-Block {
    IoWr 0x43 0x00; IoWr 0x44 0x00; IoWr 0x45 0x7E
    $t = $sw.Elapsed.TotalMilliseconds
    IoWr 0x46 0x02
    $n = 0
    while ((IoRd 0x40) -ne 0x7F) { $n++; if ($n -ge 1000) { throw 'erase: 40h never 7Fh' } }
    $busySeen = $false; $polls = 0; $seq = New-Object System.Collections.Generic.List[string]
    while ($true) {
        IoWr 0x46 0x04
        $n = 0
        while ((IoRd 0x40) -ne 0x7F) { $n++; if ($n -ge 1000) { throw 'erase: 40h never 7Fh after status' } }
        $s = IoRd 0x47
        $polls++
        $tag = '{0:X2}' -f $s
        if ($seq.Count -eq 0 -or $seq[$seq.Count - 1] -ne $tag) { $seq.Add($tag) }
        if (($s -band 1) -eq 0) { break }
        $busySeen = $true
        if ($sw.Elapsed.TotalMilliseconds - $t -gt 10000) { throw "erase: BUSY over 10 s (SR1=$tag)" }
    }
    Log ('erase 7E0000h: {0:F0} ms, {1} status reads, BUSY seen={2}, SR1 sequence: {3}' -f ($sw.Elapsed.TotalMilliseconds - $t), $polls, $busySeen, ($seq -join ' -> '))
    if (-not $busySeen) { throw 'STOP: erase was not accepted (BUSY never set)' }
}

$powered = $false
try {
    if ($RestoreFrom) { $src = [System.IO.File]::ReadAllBytes($RestoreFrom) } else { $src = [System.IO.File]::ReadAllBytes($Expect) }
    if ($src.Length -lt $BlockSize) { throw "source shorter than 64KB: $($src.Length)" }
    $want = New-Object byte[] $BlockSize
    [Array]::Copy($src, 0, $want, 0, $BlockSize)

    Log ("drained before resync: {0} bytes" -f (Drain))
    $null = Cmd 'HVER'
    Log ("drained after HVER: {0} bytes" -f (Drain))
    $null = Cmd 'LSCR'
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

    Log '--- phase 1: how fast commands settle (read and status only)'
    Measure-CommandSettle

    if (-not $RestoreFrom) {
        Log '--- phase 2: backup of 7E0000h-7EFFFFh'
        $t = $sw.Elapsed.TotalMilliseconds
        $backup = Read-Rom $Block $BlockSize
        [System.IO.File]::WriteAllBytes("$LogPath.backup.bin", $backup)
        $bad = 0; for ($i = 0; $i -lt $BlockSize; $i++) { if ($backup[$i] -ne $want[$i]) { $bad++ } }
        Log ('backup read {0:F0} ms, mismatches against -Expect: {1}' -f ($sw.Elapsed.TotalMilliseconds - $t), $bad)
        if ($bad -ne 0) { throw 'STOP: block does not match -Expect; not erasing' }
    }

    Log '--- phase 3: erase'
    Erase-Block

    Log '--- phase 4: blank check of the whole block'
    $t = $sw.Elapsed.TotalMilliseconds
    $blank = Read-Rom $Block $BlockSize
    $nonFF = 0; for ($i = 0; $i -lt $BlockSize; $i++) { if ($blank[$i] -ne 0xFF) { $nonFF++ } }
    Log ('blank read {0:F0} ms, non-FF bytes: {1}' -f ($sw.Elapsed.TotalMilliseconds - $t), $nonFF)
    if ($nonFF -ne 0) { throw 'STOP: block is not blank after erase' }

    Log '--- phase 5: write'
    $t = $sw.Elapsed.TotalMilliseconds
    Write-Rom $Block $want
    Log ('write {0:F0} ms' -f ($sw.Elapsed.TotalMilliseconds - $t))

    Log '--- phase 6: verify'
    $t = $sw.Elapsed.TotalMilliseconds
    $back = Read-Rom $Block $BlockSize
    [System.IO.File]::WriteAllBytes("$LogPath.verify.bin", $back)
    $bad = 0; $first = -1
    for ($i = 0; $i -lt $BlockSize; $i++) { if ($back[$i] -ne $want[$i]) { $bad++; if ($first -lt 0) { $first = $i } } }
    Log ('verify read {0:F0} ms, mismatches: {1}{2}' -f ($sw.Elapsed.TotalMilliseconds - $t), $bad, $(if ($first -ge 0) { (' (first at {0:X6}h: got {1:X2}h, want {2:X2}h)' -f ($Block + $first), $back[$first], $want[$first]) } else { '' }))
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
