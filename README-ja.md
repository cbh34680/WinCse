# WinCse &middot; Windows Cloud Storage Explorer

WinCse は、AWS S3 バケットを Windows Explorer に統合するアプリケーションで、S3 バケットをローカルのファイルシステムのように扱うことができます。

## 特徴
- Windows Explorer に S3 バケットを表示
- シンプルなインターフェースで簡単なファイル管理

## 動作環境
- Windows 10 以降
- [WinFsp](http://www.secfs.net/winfsp/)
- [AWS SDK for C++](https://github.com/aws/aws-sdk-cpp)

## インストール方法
1. [WinFsp](https://winfsp.dev/rel/) をインストールする。
2. [リリースページ](https://github.com/cbh34680/WinCse/releases) から WinCse (AWS SDK for C++ を含む) をダウンロードする。

## 使用方法
1. `setup/install-aws-s3.bat` を管理者権限で実行する。
2. フォーム画面が表示されたら、AWS の認証情報を入力する。
3. 「作成」ボタンを押す。
4. 表示された Explorer のディレクトリから `mount.bat` を実行する。
5. フォーム画面で選択したドライブで、Windows Explorer から S3 バケットにアクセスできるようになる。
6. `un-mount.bat` を実行すると、マウントしたドライブを解除できる。

## アンインストール方法
1. `reg-del.bat` を管理者権限で実行し、WinFsp に登録されたレジストリ情報を削除する。
2. `*.bat` ファイルがあるディレクトリを削除する。
3. 必要がなくなった場合は [WinFsp](https://winfsp.dev/rel/) をアンインストールする。

## 制限事項
- いくつかの制約については [config](./doc/conf-example.txt) を変更することで緩和可能です。
- 制限事項については [制限事項.md](./doc/limitations.md) を参照してください。

## 注意事項
- 現在のバージョンはテスト段階です。
- Windows 11 のみで動作確認済みです。
- 安定した動作を求める場合は [Rclone](https://rclone.org/) の使用を推奨します。

## ライセンス
本プロジェクトは [GPLv3](https://www.gnu.org/licenses/gpl-3.0.html) および [Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0) のもとでライセンスされています。
