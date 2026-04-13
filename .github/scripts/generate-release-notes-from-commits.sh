#!/usr/bin/env bash
# 从 conventional commit 生成 GitHub Release 正文（Markdown）。
# 分类标题与 .github/release.yml 中 changelog.categories[].title 保持一致。
# 在 CI 中由 GITHUB_OUTPUT 输出 path=；本地可 unset GITHUB_OUTPUT 则只向 stdout 打印临时文件路径。
#
# 必填环境变量：
#   RELEASE_TAG   当前 Git 标签，如 v1.2.3
#   REPO_URL      仓库根 URL，如 https://github.com/org/repo
#   IMAGE_NAME    Docker 镜像名，如 ghcr.io/org/repo
# 可选环境变量：
#   SHA_SHORT     当前发布提交的短 SHA，用于补充 Docker 标签展示
#   RELEASE_CONFIG Release 配置文件路径，默认 .github/release.yml

set -euo pipefail

: "${RELEASE_TAG:?RELEASE_TAG is required}"
: "${REPO_URL:?REPO_URL is required}"
: "${IMAGE_NAME:?IMAGE_NAME is required}"

version="${RELEASE_TAG}"
notes_file="${NOTES_FILE:-$(mktemp)}"
release_config="${RELEASE_CONFIG:-.github/release.yml}"
previous_tag="$(git describe --tags --abbrev=0 "${version}^" 2>/dev/null || true)"

if [[ ! -f "$release_config" ]]; then
  echo "Release config not found: $release_config" >&2
  exit 1
fi

if [[ -n "$previous_tag" ]]; then
  range="${previous_tag}..${version}"
  compare_url="${REPO_URL}/compare/${previous_tag}...${version}"
else
  range="$version"
  compare_url=""
fi

strip_yaml_quotes() {
  local value="$1"
  value="${value%\"}"
  value="${value#\"}"
  value="${value%\'}"
  value="${value#\'}"
  printf '%s\n' "$value"
}

declare -a excluded_authors
declare -a category_titles
declare -a category_labels
declare -a category_files
declare -i recorded_commits=0
declare -i fallback_index=-1

cleanup() {
  local file
  for file in "${category_files[@]:-}"; do
    [[ -n "$file" && -f "$file" ]] && rm -f "$file"
  done
}

trap cleanup EXIT

add_category() {
  local title="$1"
  local labels_csv="$2"
  local index="${#category_titles[@]}"

  category_titles+=("$title")
  category_labels+=("$labels_csv")
  category_files+=("$(mktemp)")

  if [[ ",${labels_csv}," == *,\*,* ]]; then
    fallback_index="$index"
  fi
}

load_release_config() {
  local current_title=""
  local current_labels=""
  local mode=""
  local exclude_mode=""
  local line=""
  local value=""

  while IFS= read -r line || [[ -n "$line" ]]; do
    case "$line" in
      "  exclude:"*)
        mode="exclude"
        exclude_mode=""
        continue
        ;;
      "  categories:"*)
        mode="categories"
        exclude_mode=""
        continue
        ;;
    esac

    case "$mode" in
      exclude)
        case "$line" in
          "    authors:"*)
            exclude_mode="authors"
            continue
            ;;
          "    labels:"*)
            exclude_mode="labels"
            continue
            ;;
        esac

        if [[ "$exclude_mode" == "authors" && "$line" == "      - "* ]]; then
          value="$(strip_yaml_quotes "${line#      - }")"
          excluded_authors+=("$value")
        fi
        ;;
      categories)
        case "$line" in
          "    - title: "*)
            if [[ -n "$current_title" ]]; then
              add_category "$current_title" "$current_labels"
            fi
            current_title="$(strip_yaml_quotes "${line#    - title: }")"
            current_labels=""
            ;;
          "      labels:"*)
            ;;
          "        - "*)
            value="$(strip_yaml_quotes "${line#        - }")"
            if [[ -n "$current_labels" ]]; then
              current_labels+=","
            fi
            current_labels+="$value"
            ;;
        esac
        ;;
    esac
  done < "$release_config"

  if [[ -n "$current_title" ]]; then
    add_category "$current_title" "$current_labels"
  fi
}

is_excluded_author() {
  local author="$1"
  local excluded=""

  for excluded in "${excluded_authors[@]:-}"; do
    [[ "$author" == "$excluded" ]] && return 0
  done

  return 1
}

label_match_score() {
  local subject="$1"
  local label="$2"

  case "$label" in
    dependencies|deps)
      [[ "$subject" == deps* || "$subject" == chore\(deps* || "$subject" == build\(deps* || "$subject" == fix\(deps* ]] && printf '100\n' || printf '0\n'
      ;;
    feat|feature)
      [[ "$subject" == feat* || "$subject" == feature* ]] && printf '90\n' || printf '0\n'
      ;;
    fix|bugfix|bug)
      [[ "$subject" == fix* || "$subject" == bugfix* || "$subject" == bug:* || "$subject" == bug\(* ]] && printf '85\n' || printf '0\n'
      ;;
    refactor)
      [[ "$subject" == refactor* ]] && printf '80\n' || printf '0\n'
      ;;
    docs)
      [[ "$subject" == docs* ]] && printf '70\n' || printf '0\n'
      ;;
    test|tests)
      [[ "$subject" == test* || "$subject" == tests* ]] && printf '70\n' || printf '0\n'
      ;;
    chore|ci|build|perf|style)
      [[ "$subject" == "$label"* ]] && printf '60\n' || printf '0\n'
      ;;
    "*")
      printf '1\n'
      ;;
    *)
      [[ "$subject" == "$label"* ]] && printf '50\n' || printf '0\n'
      ;;
  esac
}

append_entry_to_category() {
  local subject="$1"
  local entry="$2"
  local labels_csv=""
  local label=""
  local score=0
  local best_score=0
  local best_index=-1
  local index=0

  while (( index < ${#category_titles[@]} )); do
    labels_csv="${category_labels[$index]}"
    IFS=',' read -r -a labels <<< "$labels_csv"

    for label in "${labels[@]}"; do
      score="$(label_match_score "$subject" "$label")"
      if (( score > best_score )); then
        best_score="$score"
        best_index="$index"
      fi
    done

    ((index += 1))
  done

  if (( best_index < 0 )); then
    best_index="$fallback_index"
  fi

  if (( best_index >= 0 )); then
    printf '%s\n' "$entry" >> "${category_files[$best_index]}"
  fi
}

load_release_config

if (( ${#category_titles[@]} == 0 )); then
  echo "No changelog categories found in $release_config" >&2
  exit 1
fi

while IFS=$'\t' read -r sha author subject || [[ -n "$sha" ]]; do
  [[ -z "$sha" || -z "$author" || -z "$subject" ]] && continue

  if is_excluded_author "$author"; then
    continue
  fi

  case "$subject" in
    chore:\ update\ version*|chore:\ 更新版本*)
      continue
      ;;
  esac

  clean_subject="$(printf '%s' "$subject" | sed -E 's/^[[:alnum:]-]+(\([^)]*\))?!?:[[:space:]]*//')"
  short_sha="${sha:0:7}"
  entry="- ${clean_subject} ([\`${short_sha}\`](${REPO_URL}/commit/${sha}))"
  lower_subject="$(printf '%s' "$subject" | tr '[:upper:]' '[:lower:]')"
  append_entry_to_category "$lower_subject" "$entry"

  recorded_commits+=1
done < <(git log "$range" --no-merges --pretty=format:'%H%x09%an%x09%s')

{
  echo "## Changelog"
  echo
  if [[ -n "$previous_tag" ]]; then
    echo "Range: [${previous_tag}...${version}](${compare_url})"
  else
    echo "Range: initial tagged release up to ${version}"
  fi
  echo

  for index in "${!category_titles[@]}"; do
    if [[ -s "${category_files[$index]}" ]]; then
      echo "### ${category_titles[$index]}"
      cat "${category_files[$index]}"
      echo
    fi
  done

  if (( recorded_commits == 0 )); then
    echo "No release-note-worthy commits were found in this tag range."
    echo
  fi

  echo "### Docker"
  echo "- Image: \`${IMAGE_NAME}:${version}\`"
  if [[ -n "${SHA_SHORT:-}" ]]; then
    echo "- Tags: \`${version}\`, \`latest\`, \`sha-${SHA_SHORT}\`"
  else
    echo "- Tags: \`${version}\`, \`latest\`"
  fi
} > "$notes_file"

if [[ -n "${GITHUB_OUTPUT:-}" ]]; then
  {
    echo "path=${notes_file}"
  } >> "$GITHUB_OUTPUT"
else
  printf '%s\n' "$notes_file"
fi
