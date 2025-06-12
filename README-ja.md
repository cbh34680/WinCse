# WinCse &middot; Windows Cloud Storage Explorer

WinCse は、オブジェクトストレージを Windows Explorer に統合し、ローカルファイルシステムのようにバケットを管理できるアプリケーションです。

## 主な機能
- Windows のファイル共有のようにオブジェクトストレージのファイルを操作可能  
- バケットの表示名や数を調整可能  
- 読み取り専用のマウントに対応  
- AWS S3, Google Cloud Storage, に対応  
- S3 互換のオブジェクトストレージに対応  

## システム要件
- Windows 11 以上を推奨  
- [WinFsp](http://www.secfs.net/winfsp/)  
- 必要な SDK (アプリケーションに含まれています)  
  - [AWS SDK for C++](https://github.com/aws/aws-sdk-cpp)  
  - [Google Cloud Platform C++ Client Libraries](https://github.com/googleapis/google-cloud-cpp)  

## インストール手順
1. [WinFsp](https://winfsp.dev/rel/) をインストール  
2. [リリースページ](https://github.com/cbh34680/WinCse/releases) から WinCse をダウンロード  

## 使用方法
1. ストレージの種類に応じたスクリプトを**管理者権限で**実行:  
   - AWS S3 &rarr; `setup/install-aws-s3.bat`  
   - Google Cloud Storage &rarr; `setup/install-gcp-gs.bat`  
   - S3 互換オブジェクトストレージ &rarr; `setup/install-compat-s3.bat`  
2. フォーム画面が表示されたら、認証情報を入力して **Create** をクリック  
3. 表示された Explorer ディレクトリで `mount.bat` を実行してストレージをマウント  
4. Windows Explorer からバケットファイルにアクセス  
5. `un-mount.bat` を実行してドライブをアンマウント  

## アンインストール手順
1. `un-mount.bat` を実行してドライブをアンマウント  
2. `reg-del.bat` を管理者権限で実行  
3. `*.bat` ファイルを含むディレクトリを削除  
4. 不要なら **WinFsp** をアンインストール  

## 更新手順
1. ドライブをアンマウント  
2. [リリースページ](https://github.com/cbh34680/WinCse/releases) から ZIP ファイルをダウンロード・展開  
3. インストールディレクトリ内の `setup` と `x64` を上書き  

## 制限事項
- いくつかの制約は [WinCse.conf](./doc/conf-example.txt) の値を変更することで緩和できます  
- バケットの作成・削除には対応していません  
- `abort()` をエラー検出に使用しており、強制終了の可能性あり  
- 詳しくは [制限事項](./doc/limitations-ja.md) を参照  

## 注意事項
- Windows 11 でのみ動作確認済み、他のバージョンでの互換性は保証できません  
- OCI Object Storage の認証情報については [OCI_AWS_CPP_SDK_S3_Examples](https://github.com/tonymarkel/OCI_AWS_CPP_SDK_S3_Examples) を参照  
- S3 互換ストレージは [OCI Object Storage](./doc/example-oci.png), [Wasabi Hot Cloud Storage](./doc/example-wasabi.png), [Cloudflare R2](./doc/example-cloudflare.png), [Backblaze B2](./doc/example-backblaze.png), [Storj DCS](./doc/example-storj.png), [Tigris](./doc/example-tigris.png), [IDrive e2](./doc/example-idrive.png) で動作確認済み  

## ライセンス
本プロジェクトは [GPLv3](https://www.gnu.org/licenses/gpl-3.0.html) および [Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0) の下で提供されています。
