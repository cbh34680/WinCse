Set-StrictMode -Version 3.0

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

# -----------------
# args
#
if ($args.Length -eq 0) {
  Msg-Warn -Text "parameter [Type] empty"
  Exit 1
}

# -----------------
# Vars
#
$CurrentDir     = Get-Location

$Type           = $args[0]
$ExeFileName    = "WinCse.exe"
$ConfFileName   = "WinCse.conf"
$LogDirName     = "log"
$WinFspLogFName = "WinFsp.log"

$RegWinFspPath  = "HKLM:\SOFTWARE\WOW6432Node\WinFsp"

#$EnvPFX86       = [Environment]::GetFolderPath("ProgramFilesX86")
#$FsregBatPath   = "${EnvPFX86}\WinFsp\bin\fsreg.bat"
$FsregBatPath   = "%ProgramFiles(x86)%\WinFsp\bin\fsreg.bat"

$RelWorkDir     = "..\work"
$WorkDir        = [System.IO.Path]::Combine($CurrentDir, $RelWorkDir)
$WorkDir        = [System.IO.Path]::GetFullPath($WorkDir)

$RelExeDir      = "..\x64\Release"
$ExeDir         = [System.IO.Path]::Combine($CurrentDir, $RelExeDir)
$ExeDir         = [System.IO.Path]::GetFullPath($ExeDir)
$ExePath        = [System.IO.Path]::Combine($ExeDir, $ExeFileName)

$AddRegBatFName = "reg-add.bat"
$DelRegBatFName = "reg-del.bat"
$QryRegBatFName = "reg-query.bat"
$MountBatFName  = "mount.bat"
$UMountBatFName = "un-mount.bat"
$ReadmeFName    = "readme.txt"

$SwitchAdmin    = @"
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

$FontS          = New-Object System.Drawing.Font("Segoe UI", 9)
$FontM          = New-Object System.Drawing.Font("Segoe UI", 10)
$FontL          = New-Object System.Drawing.Font("Segoe UI", 11)

# -----------------
# Function
#
function Test-IsAdmin {
  $currentUser = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
  return $currentUser.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Msg-Warn {
  param (
    [string]$Text = "Something happened"
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
    [string]$Text = "Something happened"
  )
    
  [System.Windows.Forms.MessageBox]::Show(
    $Text,
    "Success",
    [System.Windows.Forms.MessageBoxButtons]::OK,
    [System.Windows.Forms.MessageBoxIcon]::Information
  )
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
  TextAlign = [System.Drawing.ContentAlignment]::MiddleLeft
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
  TextAlign = [System.Drawing.ContentAlignment]::MiddleLeft
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
  TextAlign = [System.Drawing.ContentAlignment]::MiddleLeft
}

$tbx_region = New-Object System.Windows.Forms.TextBox -Property @{
  Location = "190,122"
  Size = "170,24"
  BorderStyle = "Fixed3D"
  Font = $FontL
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
  TextAlign = [System.Drawing.ContentAlignment]::MiddleLeft
}

$cbx_drive = New-Object System.Windows.Forms.ComboBox -Property @{
  Location = "190,160"
  Size = "70,24"
  Font = $FontL
  DropDownStyle = [System.Windows.Forms.ComboBoxStyle]::DropDownList
}

# Drive Letter
$drives = Get-PSDrive -PSProvider FileSystem | Select-Object -ExpandProperty Name
$chars = @([char[]](72..86))

foreach ($ch in $chars) {
  $in_use = $false

  foreach ($drive in $drives) {
    if ($ch -eq $drive) {
      $in_use = $true
      break
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
  Font = $FontS
  TextAlign = [System.Drawing.ContentAlignment]::MiddleLeft
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
  TextAlign = [System.Drawing.ContentAlignment]::MiddleLeft
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
  Font = New-Object System.Drawing.Font("Segoe UI", 9)
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
  TextAlign = [System.Drawing.ContentAlignment]::MiddleLeft
}

$txt_exe = New-Object System.Windows.Forms.Label -Property @{
  Location = "190,240"
  Size = "330,24"
  BorderStyle = "Fixed3D"
  Text = $ExePath
  Font = $FontM
}

$btn_app = New-Object System.Windows.Forms.Button -Property @{
  Location = "525,240"
  Size = "32,24"
  Text = "..."
  Font = New-Object System.Drawing.Font("Segoe UI", 9)
}

$btn_app.Add_Click({
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
  $keyid = $tbx_keyid.Text
  $secret = $tbx_secret.Text
  $region = $tbx_region.Text
  $drive = $cbx_drive.SelectedItem
  $workdir_drive = $workdir.Substring(0, 1)
  $workdir_dir = $workdir.Substring(3)
  $info_log_dir = ";"

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

    $info_log_dir = "# log:      [${logdir}]"
    $fsreg_arg = @"
"-u %%%%1 -m %%%%2 -d 1 -D ""${WinFspLog}"" -T ""${logdir}""" "D:P(A;;RPWPLC;;;WD)"
"@

  } else {
    $fsreg_arg = @"
"-u %%%%1 -m %%%%2" "D:P(A;;RPWPLC;;;WD)"
"@
  }

  $reg_name = "WinCse.${Type}.${drive}"

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

aws_access_key_id=${keyid}
aws_secret_access_key=${secret}
region=${region}

; You can select the bucket names to display using wildcards as follows.
#bucket_filters=my-bucket-1,my-bucket-2*

; Changing the following values will affect the response.
;
; Maximum number of display objects
#max_objects=1000

; The maximum file size that can be opened
#max_filesize_mb=4

; ----------
; INFO
;
# dir:      [${workdir}]
# exe:      [${exepath}]
# mount:    [${drive}]
# net use
#  drive:   [${workdir_drive}]
#  dir:     [${workdir_dir}]
${info_log_dir}

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
         $lbl_region, $tbx_region, $lbl_drive,  $cbx_drive,  $chk_log, `
         $lbl_wkdir,  $txt_wkdir,  $btn_wkdir,               `
         $lbl_exe,    $txt_exe,    $btn_app,                 `
         $btn_reg,    $btn_exit

for ($i = 0; $i -lt $ctrls.Length; $i++) {
  $form.Controls.Add($ctrls[$i])
}

[void]$form.ShowDialog()

# EOF