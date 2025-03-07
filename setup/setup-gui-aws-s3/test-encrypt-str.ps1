Set-StrictMode -Version 3.0

$Error.Clear();

# ���W�X�g�� "HKLM:\SOFTWARE\Microsoft\Cryptography" ���� "MachineGuid" �̒l���擾
$machineGuid = Get-ItemPropertyValue -Path "HKLM:\SOFTWARE\Microsoft\Cryptography" -Name "MachineGuid"
$keyStr = $machineGuid -replace '-',''

# MachineGuid �̒l��AES �� key �Ƃ��Aiv �ɂ� key[0..16] ��ݒ�
$Key = [System.Text.Encoding]::UTF8.GetBytes($keyStr)

# �Í������镶���� ('\0' �K�v?)
$PlainText = "Hello, World!`0"

# AES�I�u�W�F�N�g�̍쐬
$Aes = [System.Security.Cryptography.AesManaged]::new()
$Aes.Key = $Key
$Aes.GenerateIV()

# �e�L�X�g���o�C�g�z��ɕϊ�
$PlainTextBytes = [System.Text.Encoding]::UTF8.GetBytes($PlainText)

# �Í�������
$Encryptor = $Aes.CreateEncryptor()
$EncryptedBytes = $Encryptor.TransformFinalBlock($PlainTextBytes, 0, $PlainTextBytes.Length)

# BASE64 ������
$EncryptedB64Str = [Convert]::ToBase64String($Aes.IV + $EncryptedBytes)

# �\�� --> DATA �� test-WinCseLib.cpp::test3() �ɓ\��t����
Write-Output "KEY: ${keyStr}"
Write-Output "DATA: ${EncryptedB64Str}"
Write-Output "Encryption completed."

# EOF