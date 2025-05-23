# WinCse &middot; Windows Cloud Storage Explorer

WinCse は、オブジェクト・ストレージを Windows Explorer に統合するアプリケーションで、バケットをローカルのファイルシステムのように扱うことができます。  

## 主な機能
- Windows ファイル共有のような感覚でオブジェクト・ストレージのファイルを操作できます。
- マウント時に表示する バケットの名前や数を調整可能です。
- 読み取り専用モードでマウントすることができます。
- AWS S3, Google Cloud Storage, OCI Object Storage で動作します。

## システム要件
- Windows 11 以降を推奨
- [WinFsp](http://www.secfs.net/winfsp/)
- [AWS SDK for C++](https://github.com/aws/aws-sdk-cpp)
- [Google Cloud Platform C++ Client Libraries](https://github.com/googleapis/google-cloud-cpp)

## インストール手順
1. [WinFsp](https://winfsp.dev/rel/) をインストールする。
2. [リリースページ](https://github.com/cbh34680/WinCse/releases) から WinCse (必要な DLL は内包しています) をダウンロードする。

# WinCse ・ Windows Cloud Storage Explorer

WinCse は、オブジェクト・ストレージを Windows Explorer に統合し、ローカルのファイルシステムのように扱えるアプリケーションです。

## 主な機能
- Windows ファイル共有のような感覚で操作可能
- 表示するバケットの名前や数を調整可能
- 読み取り専用モード対応
- **AWS S3 / Google Cloud Storage / OCI Object Storage に対応**

## システム要件
- Windows 11 以降推奨
- [WinFsp](http://www.secfs.net/winfsp/)
- 必要な SDK
  - [AWS SDK for C++](https://github.com/aws/aws-sdk-cpp)
  - [Google Cloud Platform C++ Client Libraries](https://github.com/googleapis/google-cloud-cpp)

## インストール手順
1. [WinFsp](https://winfsp.dev/rel/) をインストール
2. [リリースページ](https://github.com/cbh34680/WinCse/releases) から WinCse をダウンロード

## 使用方法
1. ストレージ種別に応じて、以下のスクリプトを **管理者権限で実行**：
   - **AWS S3** &rarr; `setup/install-aws-s3.bat`
   - **GCP GS** &rarr; `setup/install-gcp-gs.bat`
   - **OCI OS** &rarr; `setup/install-oci-os.bat`
2. フォーム画面が表示されたら、認証情報を入力し **作成** ボタンを押す。
3. Explorer のディレクトリから `mount.bat` を実行し、ストレージをマウント。
4. Windows Explorer でバケットのファイルにアクセス可能になる。
5. `un-mount.bat` を実行すると、マウントしたドライブを解除できる。

## アンインストール方法
1. マウントしたドライブをアンマウントする。
2. `reg-del.bat` を管理者権限で実行し、WinFsp に登録されたレジストリ情報を削除する。
3. `*.bat` ファイルがあるディレクトリを削除する。
4. 必要がなければ、[WinFsp](https://winfsp.dev/rel/) をアンインストールする。

## アップデート方法
1. マウントしたドライブをアンマウントする。
2. [リリースページ](https://github.com/cbh34680/WinCse/releases) から取得した zip ファイルを展開する。
3. zip ファイルに含まれている setup, x64 のディレクトリをインストール先のディレクトリに上書きする。

## 制限事項
- いくつかの制約については [設定ファイル](./doc/conf-example.txt) を変更することで緩和可能です。
- バケットの作成・削除は利用できません。
- 不具合の検出を容易にするため `abort()` を使用しており、強制終了する可能性があります。
- その他の制限事項については [制限事項](./doc/limitations-ja.md) を参照してください。

## 注意事項
- 本ソフトウェアは Windows 11 のみで動作確認されています。他のバージョンとの互換性は保証されていません。
- OCI Object Storage への接続は [OCI_AWS_CPP_SDK_S3_Examples](https://github.com/tonymarkel/OCI_AWS_CPP_SDK_S3_Examples) を参考にしています。

## ライセンス
本プロジェクトは [GPLv3](https://www.gnu.org/licenses/gpl-3.0.html) および [Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0) のもとでライセンスされています。
