﻿Set-StrictMode -Version 3.0

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$Error.Clear();

# -----------------
# Vars
#
$CurrentDir      = Get-Location

$AppName         = "WinCse"
$Type            = "aws-s3"
$ExeFileName     = "${AppName}.exe"
$ConfFileName    = "${AppName}.conf"
$LogDirName      = "log"
$WinFspLogFName  = "WinFsp.log"

$RegWinFspPath   = "HKLM:\SOFTWARE\WOW6432Node\WinFsp"

#$EnvPFX86        = [Environment]::GetFolderPath("ProgramFilesX86")
#$FsregBatPath    = "${EnvPFX86}\WinFsp\bin\fsreg.bat"
$FsregBatPath    = "%ProgramFiles(x86)%\WinFsp\bin\fsreg.bat"

$RelWorkDir      = "..\work"
$WorkDir         = [System.IO.Path]::Combine($CurrentDir, $RelWorkDir)
$WorkDir         = [System.IO.Path]::GetFullPath($WorkDir)

$RelExeDir       = "..\x64\Release"
$ExeDir          = [System.IO.Path]::Combine($CurrentDir, $RelExeDir)
$ExeDir          = [System.IO.Path]::GetFullPath($ExeDir)
$ExePath         = [System.IO.Path]::Combine($ExeDir, $ExeFileName)

$AddRegBatFName  = "reg-add.bat"
$DelRegBatFName  = "reg-del.bat"
$QryRegBatFName  = "reg-query.bat"
$MountBatFName   = "mount.bat"
$UMountBatFName  = "un-mount.bat"
$ReadmeFName     = "readme.txt"

$SwitchAdmin     = @"
@rem
@rem Get administrator rights
@rem
net session >nul 2>nul
if %errorlevel% neq 0 (
    cd "%~dp0"
    powershell.exe Start-Process -FilePath ".\%~nx0" -Verb runas
    exit
)
"@

$FontS            = New-Object System.Drawing.Font("Segoe UI", 9)
$FontM            = New-Object System.Drawing.Font("Segoe UI", 10)
$FontL            = New-Object System.Drawing.Font("Segoe UI", 11)
$LblTextAlign     = [System.Drawing.ContentAlignment]::MiddleLeft

#
# AES 暗号化オブジェクトの作成
#   Key:    レジストリの MachineGuid
#   IV:     Key[0..15]
#
$MachineGuid      = Get-ItemPropertyValue -Path "HKLM:\SOFTWARE\Microsoft\Cryptography" -Name "MachineGuid"
$SecureKeyStr     = $MachineGuid -replace '-',''
$SecureKeyBytes   = [System.Text.Encoding]::UTF8.GetBytes($SecureKeyStr)

$AesKeyId         = [System.Security.Cryptography.AesManaged]::new()
$AesKeyId.Key     = $SecureKeyBytes[0..31]
$AesKeyId.GenerateIV()
$EncryptorKeyId   = $AesKeyId.CreateEncryptor()

$AesSecret        = [System.Security.Cryptography.AesManaged]::new()
$AesSecret.Key    = $SecureKeyBytes[0..31]
$AesSecret.GenerateIV()
$EncryptorSecret  = $AesSecret.CreateEncryptor()

# -----------------
# Function
#
function Test-IsAdmin {
    $currentUser = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
    return $currentUser.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Msg-Warn {
    param (
        [Parameter(Mandatory=$true)]
        [string]$Text
    )

    [System.Windows.Forms.MessageBox]::Show(
        $Text,
        "Warning",
        [System.Windows.Forms.MessageBoxButtons]::OK,
        [System.Windows.Forms.MessageBoxIcon]::Warning
    )
}

function Msg-OK {
    param (
        [Parameter(Mandatory=$true)]
        [string]$Text
    )

    [System.Windows.Forms.MessageBox]::Show(
        $Text,
        "Success",
        [System.Windows.Forms.MessageBoxButtons]::OK,
        [System.Windows.Forms.MessageBoxIcon]::Information
    )
}

function KeyId-EncryptString {
    param (
        [Parameter(Mandatory=$true)]
        [string]$Text
    )

    $PlainTextBytes = [System.Text.Encoding]::UTF8.GetBytes("${Text}`0")
    $EncryptedBytes = $EncryptorKeyId.TransformFinalBlock($PlainTextBytes, 0, $PlainTextBytes.Length)
    $EncryptedB64Str = [Convert]::ToBase64String($AesKeyId.IV + $EncryptedBytes)

    return $EncryptedB64Str
}

function Secret-EncryptString {
    param (
        [Parameter(Mandatory=$true)]
        [string]$Text
    )

    $PlainTextBytes = [System.Text.Encoding]::UTF8.GetBytes("${Text}`0")
    $EncryptedBytes = $EncryptorSecret.TransformFinalBlock($PlainTextBytes, 0, $PlainTextBytes.Length)
    $EncryptedB64Str = [Convert]::ToBase64String($AesSecret.IV + $EncryptedBytes)

    return $EncryptedB64Str
}

# -----------------
# Check
#

# has Admin ?
if (-not (Test-IsAdmin)) {
    Msg-Warn -Text "Please run this script with administrative privileges."
    Exit 1
}

# Install WinFsp ?
if (-not (Test-Path -Path $RegWinFspPath)) {
    Msg-Warn -Text "Please install WinFsp beforehand."
    Exit 1
}

# -----------------
# Access Key Id
#
$lbl_keyid = New-Object System.Windows.Forms.Label -Property @{
    Location = "40,40"
    Size = "130,24"
    BorderStyle = "Fixed3D"
    Text = "Access Key Id"
    Font = $FontM
    TextAlign = $LblTextAlign
}

$tbx_keyid = New-Object System.Windows.Forms.TextBox -Property @{
    Location = "190,42"
    Size = "370,24"
    BorderStyle = "Fixed3D"
    Font = $FontL
}

# -----------------
# Secret Access Key
#
$lbl_secret = New-Object System.Windows.Forms.Label -Property @{
    Location = "40,80"
    Size = "130,24"
    BorderStyle = "Fixed3D"
    Text = "Secret Access Key"
    Font = $FontM
    TextAlign = $LblTextAlign
}

$tbx_secret = New-Object System.Windows.Forms.MaskedTextBox -Property @{
    Location = "190,82"
    Size = "370,24"
    BorderStyle = "Fixed3D"
    Font = $FontL
    PasswordChar = "*"
}

# -----------------
# Region
#
$lbl_region = New-Object System.Windows.Forms.Label -Property @{
    Location = "40,120"
    Size = "130,24"
    BorderStyle = "Fixed3D"
    Text = "Region"
    Font = $FontM
    TextAlign = $LblTextAlign
}

$tbx_region = New-Object System.Windows.Forms.TextBox -Property @{
    Location = "190,122"
    Size = "170,24"
    BorderStyle = "Fixed3D"
    Font = $FontL
}

# -----------------
# Encrypt
#
$chk_encrypt = New-Object System.Windows.Forms.CheckBox -Property @{
    Location = "470,120"
    Size = "100,24"
    Text = "Encrypt"
    Font = $FontM
    TextAlign = $LblTextAlign
    Checked = $true
}

# -----------------
# Mount Drive
#
$lbl_drive = New-Object System.Windows.Forms.Label -Property @{
    Location = "40,160"
    Size = "130,24"
    BorderStyle = "Fixed3D"
    Text = "Mount Drive"
    Font = $FontM
    TextAlign = $LblTextAlign
}

$cbx_drive = New-Object System.Windows.Forms.ComboBox -Property @{
    Location = "190,160"
    Size = "70,24"
    Font = $FontL
    DropDownStyle = [System.Windows.Forms.ComboBoxStyle]::DropDownList
}

# HKLM:.../WinFsp/Services/* --> regDrives
$regServices = Get-ChildItem -Path "${RegWinFspPath}\Services" | Select-Object -ExpandProperty Name
$regDrives = @()

foreach ($regService in $regServices) {
    $lastPart = $regService -split '\\' | Select-Object -Last 1
    $regDrive = $lastPart -split '\.'
    if ($regDrive.Length -eq 3) {
        if ($regDrive[0] -eq ${AppName}) {
            $regDrives += $regDrive[2]
        }
    }
}

# Filesystem Drives
$fsDrives = Get-PSDrive -PSProvider FileSystem | Select-Object -ExpandProperty Name

# Drive Letters ('H'..'V')
$chars = @([char[]](72..86))

# check used
foreach ($ch in $chars) {
    $in_use = $false

    if ($fsDrives -contains $ch) {
        $in_use = $true

    } else {
        if ($regDrives -contains $ch) {
            $in_use = $true
        }
    }

    if (-not $in_use) {
        [void]$cbx_drive.Items.Add($ch)
    }
}

if ($cbx_drive.Items.Count -ne 0) {
    $cbx_drive.SelectedIndex = 0
}

# -----------------
# Log checkbox
#
$chk_log = New-Object System.Windows.Forms.CheckBox -Property @{
    Location = "470,160"
    Size = "100,24"
    Text = "Log output"
    Font = $FontM
    TextAlign = $LblTextAlign
}

# -----------------
# Work Directory
#
$lbl_wkdir = New-Object System.Windows.Forms.Label -Property @{
    Location = "40,200"
    Size = "130,24"
    BorderStyle = "Fixed3D"
    Text = "Working Directory"
    Font = $FontM
    TextAlign = $LblTextAlign
}

$txt_wkdir = New-Object System.Windows.Forms.Label -Property @{
    Location = "190,200"
    Size = "330,24"
    BorderStyle = "Fixed3D"
    Text = $WorkDir
    Font = $FontM
}

$btn_wkdir = New-Object System.Windows.Forms.Button -Property @{
    Location = "525,200"
    Size = "32,24"
    Text = "..."
    Font = $FontS
}

$btn_wkdir.Add_Click({
    $selpath = $txt_wkdir.Text

    if (-not (Test-Path -Path $selpath)) {
        $selpath = $CurrentDir
    }

    $dlg = New-Object System.Windows.Forms.FolderBrowserDialog
    $dlg.Description = "Please select a folder"
    $dlg.SelectedPath = $selpath

    $result = $dlg.ShowDialog()

    if ($result -eq [System.Windows.Forms.DialogResult]::OK) {
        $txt_wkdir.Text = $dlg.SelectedPath
    }
})

# -----------------
# Application
#
$lbl_exe = New-Object System.Windows.Forms.Label -Property @{
    Location = "40,240"
    Size = "130,24"
    BorderStyle = "Fixed3D"
    Text = $ExeFileName
    Font = $FontM
    TextAlign = $LblTextAlign
}

$txt_exe = New-Object System.Windows.Forms.Label -Property @{
    Location = "190,240"
    Size = "330,24"
    BorderStyle = "Fixed3D"
    Text = $ExePath
    Font = $FontM
}

$btn_exe = New-Object System.Windows.Forms.Button -Property @{
    Location = "525,240"
    Size = "32,24"
    Text = "..."
    Font = $FontS
}

$btn_exe.Add_Click({
    $dlg = New-Object System.Windows.Forms.OpenFileDialog
    $dlg.Filter = "Executable Files (*.exe)|*.exe|All Files (*.*)|*.*"

    $inidir = Split-Path $txt_exe -Parent
    $ininame = Split-Path $txt_exe -Leaf

    $dlg.InitialDirectory = $inidir
    $dlg.FileName = $ininame

    $result = $dlg.ShowDialog()
    if ($result -eq [System.Windows.Forms.DialogResult]::OK) {
        $txt_exe.Text = $dlg.FileName
    }
})


# -----------------
# Create
#
$btn_reg = New-Object System.Windows.Forms.Button -Property @{
    Location = "410,375"
    Size = "72,30"
    Text = "Create"
    Font = $FontM
}

$btn_reg.Add_Click({
    $workdir = $txt_wkdir.Text
    $conf_path = "${workdir}\${ConfFileName}"

    # check conf
    if (Test-Path -Path $conf_path) {
        $result = Msg-Warn -Text "${conf_path}: The file exists. Please select another directory."
        return
    }

    # make workdir
    if (-not (Test-Path -Path $workdir)) {
        New-Item -Path $workdir -ItemType Directory -Force
    }

    if (-not (Test-Path -Path $workdir)) {
        Msg-Warn -Text "${workdir}: The directory does not exist."
        return
    }

    $exepath = $txt_exe.Text
    if (-not (Test-Path -Path $exepath)) {
        Msg-Warn -Text "${exepath}: The file does not exist."
        return
    }

    # Values
    $keyid = $tbx_keyid.Text.Trim()
    $secret = $tbx_secret.Text.Trim()
    $region = $tbx_region.Text.Trim()
    $drive = $cbx_drive.SelectedItem
    $workdir_drive = $workdir.Substring(0, 1)
    $workdir_dir = $workdir.Substring(3)
    $info_log_dir = ";"

    if ($chk_encrypt) {
        if ($keyid -ne "") {
            $keyid = "{aes256}" + (KeyId-EncryptString -Text $keyid)
        }

        if ($secret -ne "") {
            $secret = "{aes256}" + (Secret-EncryptString -Text $secret)
        }
    }

    if ($chk_log.Checked) {
        $logdir = "${workdir}\${Type}\${LogDirName}"
        $WinFspLog = "${logdir}\${WinFspLogFName}"

        if (-not (Test-Path -Path $logdir)) {
            New-Item -Path $logdir -ItemType Directory -Force
        }

        if (-not (Test-Path -Path $logdir)) {
            Msg-Warn -Text "${logdir}: The directory does not exist."
            return
        }

        $info_log_dir = "# log:          [${logdir}]"
        $fsreg_arg = @"
"-u %%%%1 -m %%%%2 -d 1 -D ""${WinFspLog}"" -T ""${logdir}""" "D:P(A;;RPWPLC;;;WD)"
"@

    } else {
        $fsreg_arg = @"
"-u %%%%1 -m %%%%2" "D:P(A;;RPWPLC;;;WD)"
"@
    }

    $reg_name = "${AppName}.${Type}.${drive}"

    # Write "reg-add.bat"
    $add_reg_bat = @"
@echo off

${SwitchAdmin}

@echo on
call "${FsregBatPath}" ${reg_name} "${exepath}" ${fsreg_arg}
@pause
"@

    Set-Content -Path "${workdir}\${AddRegBatFName}" -Value $add_reg_bat

    # Write "reg-del.bat"
    $del_reg_bat = @"
@echo off

${SwitchAdmin}

@echo on
if exist ${drive}:\ ( net use ${drive}: /delete )
call "${FsregBatPath}" -u ${reg_name}
@pause
"@

    Set-Content -Path "${workdir}\${DelRegBatFName}" -Value $del_reg_bat

    # Write "mount.bat
    $mount_bat = @"
@echo off

${SwitchAdmin}

@echo on
if exist ${drive}:\ net use ${drive}: /delete
net use ${drive}: "\\${reg_name}\${workdir_drive}$\${workdir_dir}"
@pause
"@

    Set-Content -Path "${workdir}\${MountBatFName}" -Value $mount_bat

    # Write "reg-query.bat"
    $qry_reg_bat = @"
@echo on
reg query HKLM\Software\WinFsp\Services\${reg_name} /s /reg:32
@pause
"@

    Set-Content -Path "${workdir}\${QryRegBatFName}" -Value $qry_reg_bat

    # Write "un-mount.bat"
    $umount_bat = @"
@echo off

if not exist ${drive}:\ (
  echo Cloud storage is not mounted.
  pause
  exit
)

${SwitchAdmin}

echo on
net use ${drive}: /delete
pause
"@

    Set-Content -Path "${workdir}\${UMountBatFName}" -Value $umount_bat

    # Write "readme.txt"
    $readme = @"
Description of the files in this directory

* WinCse.conf
    Authentication information for the cloud storage referenced by the WinCse application.

* reg-add.bat
    Runs fsreg.bat included with WinFsp to register the WinCse application in the Windows registry.

* reg-del.bat
    Deletes the information registered by reg-add.bat.

* reg-query.bat
    Displays the information registered by reg-add.bat.

* mount.bat
    Connects to the cloud storage on the drive specified during setup.

* un-mount.bat
    Disconnects the drive mounted by mount.bat.

"@

    Set-Content -Path "${workdir}\${ReadmeFName}" -Value $readme

    # Write conf
    $conf = @"
;
; ${conf_path}
;
[default]
type=${Type}

; AWS client credentials
aws_access_key_id=${keyid}
aws_secret_access_key=${secret}
region=${region}

; You can select the bucket names to display using wildcards as follows.
; default: Not set
#bucket_filters=my-bucket-1,my-bucket-2*

; Delete the cache files after the upload is completed.
; valid value: 0 or non-zero
; default: 0 (Do not delete)
#delete_after_upload=0

; Assign the read-only attribute to the file.
; valid value: 0 or non-zero
; default: 0 (Read and write)
#readonly=0

;!
;! Changing the following values will affect the response.
;!
; Maximum number of display buckets.
; valid range: 0 (No restrictions) to INT_MAX
; default: 8
#max_display_buckets=8

; Maximum number of display objects.
; valid range: 0 to INT_MAX
; default: 1000
#max_display_objects=1000

; Bucket cache expiration period.
; valid range: 1 to 1440 (1 day)
; default: 20
#bucket_cache_expiry_min=20

; Object cache expiration period.
; valid range: 1 to 60 (1 hour)
; default: 3
#object_cache_expiry_min=3

; The duration for retaining cache files.
; valid range: 1 to 10080 (1 week)
; default: 360 (6 hours)
#cache_file_retention_min=360

; Handle file timestamps strictly
; valid value: 0 or non-zero
; default: 0 (Not strict)
#strict_file_timestamp=0

; File names matching the following regular expression patterns will be ignored.
; default: \b(desktop\.ini|autorun\.inf|(eh)?thumbs\.db|AlbumArtSmall\.jpg|folder\.(ico|jpg|gif)|\.DS_Store)$
#re_ignore_patterns=\b(desktop\.ini|autorun\.inf|(eh)?thumbs\.db|AlbumArtSmall\.jpg|folder\.(ico|jpg|gif)|\.DS_Store)$

; ----------
; INFO
;
# dir:          [${workdir}]
# exe:          [${exepath}]
# mount:        [${drive}]
# net use
#    drive:     [${workdir_drive}]
#    dir:       [${workdir_dir}]
${info_log_dir}

# EOF
"@

    Set-Content -Path $conf_path -Value $conf

    # fsreg.bat
    Start-Process -FilePath "${workdir}\${AddRegBatFName}" -Wait

    $reg_path = "${RegWinFspPath}\Services\${reg_name}"

    if (-not (Test-Path -Path $reg_path)) {
        Msg-Warn -Text "Failed to subscribe."
        return
    }

    Msg-OK -Text "Successfully subscribed."

    $form.Close()

    # Open explorer
    Start-Process explorer.exe $workdir

})

# -----------------
# Close
#
$btn_exit = New-Object System.Windows.Forms.Button -Property @{
    Location = "490,375"
    Size = "70,30"
    Text = "Close"
    Font = $FontM
}

$btn_exit.Add_Click({
    $form.Close()
})

# -----------------
# Main Form
#
$form = New-Object System.Windows.Forms.Form -Property @{
    Text = "AWS Credentials and Application Settings"
    Size = "640,480"
    FormBorderStyle = "Fixed3D"
    AcceptButton = $btn_exit
}

$ctrls = $lbl_keyid,  $tbx_keyid,  $lbl_secret, $tbx_secret, `
         $lbl_region, $tbx_region, $chk_encrypt,
         $lbl_drive,  $cbx_drive,  $chk_log, `
         $lbl_wkdir,  $txt_wkdir,  $btn_wkdir,                             `
         $lbl_exe,    $txt_exe,    $btn_exe,                                 `
         $btn_reg,    $btn_exit

for ($i = 0; $i -lt $ctrls.Length; $i++) {
    $form.Controls.Add($ctrls[$i])
}

[void]$form.ShowDialog()

# EOF