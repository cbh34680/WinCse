Set-StrictMode -Version 3.0

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$Error.Clear();

# -----------------
# Vars
#
$CurrentDir      = Get-Location
$DllType         = "aws-s3"
$AppName         = "WinCse"
$ExeFileName     = "${AppName}.exe"
$ConfFileName    = "${AppName}.conf"
$LogDirName      = "log"
$WinFspLogFName  = "WinFsp.log"
$RegWinFspPath   = "HKLM:\SOFTWARE\WOW6432Node\WinFsp"
$FsregBatPath    = "%ProgramFiles(x86)%\WinFsp\bin\fsreg.bat"
$RelWorkDir      = "..\work"
$WorkDir         = [System.IO.Path]::Combine($CurrentDir, $RelWorkDir)
$WorkDir         = [System.IO.Path]::GetFullPath($WorkDir)
$RelExeDir       = "..\x64\Release"
$ExeDir          = [System.IO.Path]::Combine($CurrentDir, $RelExeDir)
$ExeDir          = [System.IO.Path]::GetFullPath($ExeDir)
$ExePath         = [System.IO.Path]::Combine($ExeDir, $ExeFileName)

$RegAddBatFName    = "reg-add.bat"
$RegAddLogBatFName = "reg-add-log.bat"
$RegDelBatFName    = "reg-del.bat"
$RegQryBatFName    = "reg-query.bat"
$MountBatFName     = "mount.bat"
$UMountBatFName    = "un-mount.bat"
$TestBootBatFName  = "test-boot.bat"
$ReadmeFName       = "readme.txt"

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

if (-not (Test-Path -Path "${RegWinFspPath}\Services")) {
    New-Item -Path "${RegWinFspPath}\Services" -Force
}

# -----------------
# Access Key Id
#
$lbl_keyid = New-Object System.Windows.Forms.Label -Property @{
    Location = "50,35"
    Size = "130,24"
    BorderStyle = "Fixed3D"
    Text = "Access Key Id"
    Font = $FontM
    TextAlign = $LblTextAlign
}

$tbx_keyid = New-Object System.Windows.Forms.TextBox -Property @{
    Location = "200,35"
    Size = "370,24"
    BorderStyle = "Fixed3D"
    Font = $FontL
}

# -----------------
# Secret Access Key
#
$lbl_secret = New-Object System.Windows.Forms.Label -Property @{
    Location = "50,75"
    Size = "130,24"
    BorderStyle = "Fixed3D"
    Text = "Secret Access Key"
    Font = $FontM
    TextAlign = $LblTextAlign
}

$tbx_secret = New-Object System.Windows.Forms.MaskedTextBox -Property @{
    Location = "200,75"
    Size = "370,24"
    BorderStyle = "Fixed3D"
    Font = $FontL
    PasswordChar = "*"
}

# -----------------
# Encrypt
#
$chk_encrypt = New-Object System.Windows.Forms.CheckBox -Property @{
    Location = "495,105"
    Size = "100,24"
    Text = "Encrypt"
    Font = $FontM
    TextAlign = $LblTextAlign
    Checked = $true
    Enabled = $true
}

# -----------------
# Region
#
$lbl_region = New-Object System.Windows.Forms.Label -Property @{
    Location = "50,115"
    Size = "130,24"
    BorderStyle = "Fixed3D"
    Text = "Region"
    Font = $FontM
    TextAlign = $LblTextAlign
}

$tbx_region = New-Object System.Windows.Forms.TextBox -Property @{
    Location = "200,115"
    Size = "170,24"
    BorderStyle = "Fixed3D"
    Font = $FontL
}

# -----------------
# Namespace
#
$lbl_ns = New-Object System.Windows.Forms.Label -Property @{
    Location = "50,155"
    Size = "130,24"
    BorderStyle = "Fixed3D"
    #Text = "Namespace"
    Font = $FontM
    TextAlign = $LblTextAlign
}

$tbx_ns = New-Object System.Windows.Forms.TextBox -Property @{
    Location = "200,155"
    Size = "170,24"
    BorderStyle = "Fixed3D"
    Font = $FontL
    Enabled = $false
}

# -----------------
# Mount Drive
#
$lbl_drive = New-Object System.Windows.Forms.Label -Property @{
    Location = "50,195"
    Size = "130,24"
    BorderStyle = "Fixed3D"
    Text = "Mount Drive"
    Font = $FontM
    TextAlign = $LblTextAlign
}

$cbx_drive = New-Object System.Windows.Forms.ComboBox -Property @{
    Location = "200,195"
    Size = "70,24"
    Font = $FontL
    DropDownStyle = [System.Windows.Forms.ComboBoxStyle]::DropDownList
}

# HKLM:***/WinFsp/Services/* --> regDrives
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
# Work Directory
#
$lbl_wkdir = New-Object System.Windows.Forms.Label -Property @{
    Location = "50,235"
    Size = "130,24"
    BorderStyle = "Fixed3D"
    Text = "Working Directory"
    Font = $FontM
    TextAlign = $LblTextAlign
}

$txt_wkdir = New-Object System.Windows.Forms.Label -Property @{
    Location = "200,235"
    Size = "330,24"
    BorderStyle = "Fixed3D"
    Text = $WorkDir
    Font = $FontM
}

$btn_wkdir = New-Object System.Windows.Forms.Button -Property @{
    Location = "535,234"
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
    Location = "50,275"
    Size = "130,24"
    BorderStyle = "Fixed3D"
    Text = $ExeFileName
    Font = $FontM
    TextAlign = $LblTextAlign
}

$txt_exe = New-Object System.Windows.Forms.Label -Property @{
    Location = "200,275"
    Size = "330,24"
    BorderStyle = "Fixed3D"
    Text = $ExePath
    Font = $FontM
}

$btn_exe = New-Object System.Windows.Forms.Button -Property @{
    Location = "535,274"
    Size = "32,24"
    Text = "..."
    Font = $FontS
}

$btn_exe.Add_Click({
    $dlg = New-Object System.Windows.Forms.OpenFileDialog
    $dlg.Filter = "Executable Files (*.exe)|WinCse.exe|All Files (*.*)|*.*"

    $inidir = Split-Path $txt_exe.Text -Parent
    $ininame = Split-Path $txt_exe.Text -Leaf

    if (-not (Test-Path -Path $inidir)) {
        $inidir = $CurrentDir
    }

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

    # リージョンは必須

    if ([string]::IsNullOrEmpty($tbx_region.Text.Trim())) {

        Msg-Warn -Text "The Region value cannot be left empty."
        $tbx_region.Focus()
        return
    }

    # Values
    $cguid = [guid]::NewGuid().ToString()
    $keyid = $tbx_keyid.Text.Trim()
    $secret = $tbx_secret.Text.Trim()
    $region = $tbx_region.Text.Trim()
    $drive = $cbx_drive.SelectedItem
    $workdir_drive = $workdir.Substring(0, 1)
    $workdir_dir = $workdir.Substring(3)
    $info_log_dir = ";"

    if ($chk_encrypt.Checked) {
        if (-not [string]::IsNullOrEmpty($keyid)) {
            $keyid = "{aes256}" + (KeyId-EncryptString -Text $keyid)
        }

        if (-not [string]::IsNullOrEmpty($secret)) {
            $secret = "{aes256}" + (Secret-EncryptString -Text $secret)
        }
    }

    $logdir = "${workdir}\${DllType}\${LogDirName}"
    $WinFspLog = "${logdir}\${WinFspLogFName}"

    if (-not (Test-Path -Path $logdir)) {
        New-Item -Path $logdir -ItemType Directory -Force
    }

    if (-not (Test-Path -Path $logdir)) {
        Msg-Warn -Text "${logdir}: The directory does not exist."
        return
    }

    $info_log_dir = "# log:          [${logdir}]"

    $reg_name = "${AppName}.${DllType}.${drive}"

    $win_acl = "D:P(A;;RPWPLC;;;WD)"

    #
    # Create "reg-add.bat"
    #
    $add_reg_bat = @"
@echo off

${SwitchAdmin}

@echo on
call "${FsregBatPath}" ${reg_name} "${exepath}" "-u %%%%1 -m %%%%2 -T ""${logdir}"" " "${win_acl}"
@pause
"@

    Set-Content -Path "${workdir}\${RegAddBatFName}" -Value $add_reg_bat

    #
    # Create "reg-add-logging.bat"
    #
    $add_reg_log_bat = @"
@echo off

${SwitchAdmin}

@echo on
call "${FsregBatPath}" ${reg_name} "${exepath}" "-u %%%%1 -m %%%%2 -d 1 -D ""${WinFspLog}"" -T ""${logdir}"" " "${win_acl}"
@pause
"@

    Set-Content -Path "${workdir}\${RegAddLogBatFName}" -Value $add_reg_log_bat

    #
    # Create "reg-del.bat"
    #
    $del_reg_bat = @"
@echo off

${SwitchAdmin}

@echo on
if exist ${drive}:\ ( net use ${drive}: /delete )
call "${FsregBatPath}" -u ${reg_name}
@pause
"@

    Set-Content -Path "${workdir}\${RegDelBatFName}" -Value $del_reg_bat

    #
    # Create "mount.bat
    #
    $mount_bat = @"
@echo on
if exist ${drive}:\ net use ${drive}: /delete
net use ${drive}: "\\${reg_name}\${workdir_drive}$\${workdir_dir}" /persistent:no
@pause
"@

    Set-Content -Path "${workdir}\${MountBatFName}" -Value $mount_bat

    #
    # Create "reg-query.bat"
    #
    $qry_reg_bat = @"
@echo on
reg query HKLM\Software\WinFsp\Services\${reg_name} /s /reg:32
@pause
"@

    Set-Content -Path "${workdir}\${RegQryBatFName}" -Value $qry_reg_bat

    #
    # Create "un-mount.bat"
    #
    $umount_bat = @"
@echo off

if not exist ${drive}:\ (
  echo Cloud storage is not mounted.
  pause
  exit
)

echo on
net use ${drive}: /delete
pause
"@

    Set-Content -Path "${workdir}\${UMountBatFName}" -Value $umount_bat

    #
    # Create "test-boot.bat"
    #
    $test_boot_bat = @"
@echo off

${SwitchAdmin}

@echo on
"${exepath}" -C 1 -u \${reg_name}\${workdir_drive}$\${workdir_dir} -m ${drive}: -d 1 -D "${WinFspLog}" -T "${logdir}" > "${workdir}\test-boot.log"

pause
"@

    Set-Content -Path "${workdir}\${TestBootBatFName}" -Value $test_boot_bat

    #
    # Create "readme.txt"
    #
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

    #
    # Create WinCse.conf
    #
    $conf = @"
;
; ${conf_path}
;
[default]
type=${DllType}

; AWS client credentials
; Note: Only valid on the computer it was created on.
aws_access_key_id=${keyid}
aws_secret_access_key=${secret}
region=${region}

; Client Identifier
; default: Not set (Generated at runtime)
#client_guid=${cguid}

; You can select the bucket names to display using wildcards as follows.
; default: Not set
#bucket_filters=my-bucket-1 my-bucket-2*

; Delete the cache files after the upload is completed.
; valid value: 0 (Do not delete), 1 (Delete after upload)
; default: 0 
#delete_after_upload=0

; Set the file as read-only.
; valid value: 0 (Read and write), 1 (Read only)
; default: 0
#readonly=0

;!
;! WARNING: Changing the following settings may affect system behavior.
;!

; Bucket cache expiration period.
; valid range: 1 to 1440 (1 day)
; default: 20
#bucket_cache_expiry_min=20

; Cache file retention period.
; valid range: 1 to 10080 (1 week)
; default: 60
#cache_file_retention_min=60

; Conditions for deleting a directory.
; valid value: 1 (No Subdirectories) or 2 (Empty Directory)
; default: 2
#delete_dir_condition=2

; Specifies the number of threads used for file I/O operations.
; valid range: 1 to 32
; default: Calculated based on the number of CPU cores
#file_io_threads=8

; Maximum retry count for API execution
; Note: Added after v0.250512.1345
; valid range: 0 to 5
; default: 3
#max_api_retry_count=3

; Maximum number of display buckets.
; valid range: 0 (No restrictions) to INT_MAX
; default: 8
#max_display_buckets=8

; Maximum number of display objects.
; valid range: 0 to INT_MAX
; default: 1000
#max_display_objects=1000

; Object cache expiration period.
; valid range: 1 to 60 (1 hour)
; default: 5
#object_cache_expiry_min=5

; Strictly enforce bucket regions.
; valid value: 0 or non-zero
; default: 0 (Not strict)
#strict_bucket_region=0

; Strictly enforce file timestamps.
; valid value: 0 or non-zero
; default: 0 (Not strict)
#strict_file_timestamp=0

; Specifies the size of data to be read during a transfer operation.
; valid range: 5 (5 MiB) to 100 (100 MiB)
; default: 10
#transfer_read_size_mib=10

; Specifies the size of data to be written during a transfer operation.
; valid range: 5 (5 MiB) to 100 (100 MiB)
; default: 10
#transfer_write_size_mib=10

; Files that match the following regex patterns will be ignored.
; default: Empty (Don't ignore)
re_ignore_patterns=\\(desktop\.ini|autorun\.inf|(eh)?thumbs\.db|AlbumArtSmall\.jpg|folder\.(ico|jpg|gif))$

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

    #
    # Execute fsreg.bat
    #
    Start-Process -FilePath "${workdir}\${RegAddBatFName}" -Wait

    $reg_path = "${RegWinFspPath}\Services\${reg_name}"

    if (-not (Test-Path -Path $reg_path)) {
        Msg-Warn -Text "Failed to subscribe."
        return
    }

    Msg-OK -Text "Successfully subscribed."

    $form.Close()

    #
    # Open explorer
    #
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
    Text = "Credentials and Application Settings - ${DllType}"
    Size = "640,480"
    FormBorderStyle = "Fixed3D"
    AcceptButton = $btn_exit
}

$ctrls = $lbl_keyid,  $tbx_keyid, 
         $lbl_secret, $tbx_secret,
         $lbl_region, $tbx_region,
         $lbl_ns,     $tbx_ns,
         $chk_encrypt,
         $lbl_drive,  $cbx_drive,
         $lbl_wkdir,  $txt_wkdir,  $btn_wkdir,                             `
         $lbl_exe,    $txt_exe,    $btn_exe,                                 `
         $btn_reg,    $btn_exit

for ($i = 0; $i -lt $ctrls.Length; $i++) {
    $form.Controls.Add($ctrls[$i])
}

[void]$form.ShowDialog()

# EOF