# MkDocs 文档

本目录包含 MkDocs 文档源文件。API 文档由仓库根目录下的
`generate_api_docs.sh` 在构建前生成。

本地预览：

```bash
cd /path/to/repo
./generate_api_docs.sh
cd mkdocs
mkdocs serve
```

CI 会在推送到 `main` 分支或手动触发时构建并发布文档站点。
