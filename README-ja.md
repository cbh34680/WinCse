# WinCse &middot; Windows Cloud Storage Explorer

WinCse �́AAWS S3 �o�P�b�g�� Windows Explorer �ɓ�������A�v���P�[�V�����ŁAS3 �o�P�b�g�����[�J���̃t�@�C���V�X�e���̂悤�Ɉ������Ƃ��ł��܂��B

## ��ȋ@�\
- Windows �t�@�C�����L�̂悤�Ȋ��o�� S3 ��̃t�@�C���𑀍�ł��܂��B
- �l�b�g���[�N�h���C�u�ɕ\������ S3 �o�P�b�g�𖼑O��\�����Œ����\�ł��B
- ���[�h�I�����[�ł̃}�E���g���s���܂��B

## �V�X�e���v��
- Windows 10 �ȍ~��z��
- [WinFsp](http://www.secfs.net/winfsp/)
- [AWS SDK for C++](https://github.com/aws/aws-sdk-cpp)

## �C���X�g�[���菇
1. [WinFsp](https://winfsp.dev/rel/) ���C���X�g�[������B
2. [�����[�X�y�[�W](https://github.com/cbh34680/WinCse/releases) ���� WinCse (AWS SDK for C++ �����) ���_�E�����[�h����B

## �g�p���@
1. `setup/install-aws-s3.bat` ���Ǘ��Ҍ����Ŏ��s����B
2. �t�H�[����ʂ��\�����ꂽ��AAWS �̔F�؏�����͂���B
3. `�쐬` �{�^���������B
4. �\�����ꂽ Explorer �̃f�B���N�g������ `mount.bat` �����s����B
5. �t�H�[����ʂőI�������h���C�u�ŁAWindows Explorer ���� S3 �o�P�b�g�ɃA�N�Z�X�ł���悤�ɂȂ�B
6. `un-mount.bat` �����s����ƁA�}�E���g�����h���C�u�������ł���B

## �A���C���X�g�[�����@
1. �}�E���g�����h���C�u���A���}�E���g����B
2. `reg-del.bat` ���Ǘ��Ҍ����Ŏ��s���AWinFsp �ɓo�^���ꂽ���W�X�g�������폜����B
3. `*.bat` �t�@�C��������f�B���N�g�����폜����B
4. �K�v�̖����ꍇ�� [WinFsp](https://winfsp.dev/rel/) ���A���C���X�g�[������B

## ��������
- �������̐���ɂ��Ă� [�ݒ�t�@�C��](./doc/conf-example.txt) ��ύX���邱�ƂŊɘa�\�ł��B
- �o�P�b�g�̍쐬�E�폜�Ȃǂ̑���͍s���܂���B
- �s������m���₷�����邽�߁Aabort() ���g�p���Ă���̂ŋ����I������\��������܂��B
- ���̑��̐��������ɂ��Ă� [��������](./doc/limitations-ja.md) ���Q�Ƃ��Ă��������B

## ���ӎ���
- Windows 11 �݂̂œ���m�F�ς݂ł��B

## ���C�Z���X
�{�v���W�F�N�g�� [GPLv3](https://www.gnu.org/licenses/gpl-3.0.html) ����� [Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0) �̂��ƂŃ��C�Z���X����Ă��܂��B
