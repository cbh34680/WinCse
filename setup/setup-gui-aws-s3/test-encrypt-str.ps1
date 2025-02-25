Set-StrictMode -Version 3.0

$Error.Clear();

# レジストリ "HKLM:\SOFTWARE\Microsoft\Cryptography" から "MachineGuid" の値を取得
$machineGuid = Get-ItemPropertyValue -Path "HKLM:\SOFTWARE\Microsoft\Cryptography" -Name "MachineGuid"
$keyStr = $machineGuid -replace '-',''

# MachineGuid の値をAES の key とし、iv には key[0..16] を設定
$Key = [System.Text.Encoding]::UTF8.GetBytes($keyStr)
$IV = $Key[0..15]

# 暗号化する文字列 ('\0' 必要?)
$PlainText = "Hello, World!`0"

# AESオブジェクトの作成
$Aes = [System.Security.Cryptography.AesManaged]::new()
$Aes.Key = $Key
$Aes.IV = $IV

# テキストをバイト配列に変換
$PlainTextBytes = [System.Text.Encoding]::UTF8.GetBytes($PlainText)

# 暗号化処理
$Encryptor = $Aes.CreateEncryptor()
$EncryptedBytes = $Encryptor.TransformFinalBlock($PlainTextBytes, 0, $PlainTextBytes.Length)

# BASE64 符号化
$EncryptedB64Str = [Convert]::ToBase64String($EncryptedBytes)

# 表示 --> DATA を test-WinCseLib.cpp::test3() に貼り付ける
Write-Output "KEY: ${keyStr}"
Write-Output "DATA: ${EncryptedB64Str}"
Write-Output "Encryption completed."

# EOF