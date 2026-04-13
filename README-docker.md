# subconverter Docker 使用说明

本文档用于说明如何使用本项目的 Docker 镜像、如何覆盖默认配置，以及当前仓库采用的镜像发布方式。

## 直接运行

```bash
# 后台运行容器，并将容器内 25500 端口映射到宿主机 25500
docker run -d --restart=always -p 25500:25500 ghcr.io/leroy20317/subconverter:latest

# 检查服务是否正常启动
curl http://localhost:25500/version

# 如果看到 `subconverter vx.x.x backend`，说明服务已经正常运行
```

## docker-compose 方式

```yaml
---
version: '3'
services:
  subconverter:
    image: ghcr.io/leroy20317/subconverter:latest
    container_name: subconverter
    ports:
      - "15051:25500"
    restart: always
```

## 容器内更新 `pref` 配置

如果你想通过接口更新容器内的 `pref` 配置，可以使用：

```bash
# 直接把配置文件内容作为请求体提交
curl --data-binary @pref.toml "http://localhost:25500/updateconf?type=direct&token=password"
```

说明：

- `token=password` 只是示例，请按你的实际配置替换。
- 当前代码的 `/updateconf` 会把整个请求体直接写入当前 `pref` 文件，然后重新读取配置。
- `type` 目前只接受 `form` 或 `direct`，两者在当前实现中都会直接写入请求体内容。

## 基于官方镜像覆盖默认文件

如果你想使用自己的 `pref`、规则、片段或 profile 文件，可以在官方镜像之上构建自定义镜像。

示例 `Dockerfile`：

```dockerfile
# 基于本项目发布的最新镜像
FROM ghcr.io/leroy20317/subconverter:latest

# 假设你的替换文件都放在 replacements/ 目录
# 镜像内程序工作目录为 /base，目录结构与仓库 base/ 目录一致
COPY replacements/ /base/

# 暴露容器内部端口
EXPOSE 25500
```

然后执行：

```bash
# 构建自定义镜像
docker build -t subconverter-custom:latest .

# 运行自定义镜像
docker run -d --restart=always -p 25500:25500 subconverter-custom:latest

# 检查服务状态
curl http://localhost:25500/version
```

## 镜像仓库

当前项目默认发布到 GitHub Container Registry：

```txt
ghcr.io/leroy20317/subconverter
```

常见标签包括：

- `latest`
- `vX.Y.Z`
- `sha-<commit>`

## 版本发布

- 基础版本号定义在 [`src/version.h`](./src/version.h)。
- 当 `master` 上的 `src/version.h` 版本号发生变化时，`tag-on-version-change.yml` 会自动创建对应的 `vX.Y.Z` tag。
- `docker.yml` 会在该 tag 被推送后自动发布镜像，并附带 `vX.Y.Z`、`latest` 与 `sha-<commit>` 标签。
- Docker 镜像推送成功后，工作流会自动创建同名的 GitHub Release，并使用 GitHub 自动生成发布说明。
- 如需补发失败的版本镜像，可以手动运行 `docker.yml`，但必须选择对应的版本 tag 作为 ref。
- 发布流程使用默认 `GITHUB_TOKEN`，不需要额外配置发布密钥。

## 相关文档

- 总览文档：[README.md](./README.md)
