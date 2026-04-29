#!/usr/bin/env bash
set -euo pipefail

# Reproducible local build helper for this Flutter checkout.
# Defaults to a local host release build. Outputs are separate per variant under
# engine/src/out/<variant>, so debug/profile/release builds can coexist.
#
# Useful overrides:
#   SKIP_SYNC=1 ./build.sh
#   MODE=debug_unopt ./build.sh
#   MODE=debug ./build.sh
#   MODE=profile ./build.sh
#   JOBS=1 ./build.sh
#   ENABLE_LTO=1 ./build.sh
#   CACHE_ONLY=1 ./build.sh

CHECKOUT_REF="${CHECKOUT_REF:-}"
LOCAL_BRANCH="${LOCAL_BRANCH:-local-skia-vulkan}"
REMOTE_URL="${REMOTE_URL:-https://github.com/flutter/flutter.git}"
MODE="${MODE:-release}"
NINJA_TARGET="${NINJA_TARGET:-}"
JOBS="${JOBS:-}"
SKIP_SYNC="${SKIP_SYNC:-0}"
ENABLE_LTO="${ENABLE_LTO:-0}"
LOCAL_FLUTTER_VERSION="${LOCAL_FLUTTER_VERSION:-3.42.0-99.0.pre}"
CACHE_ONLY="${CACHE_ONLY:-0}"

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
DEPOT_TOOLS_DIR="${DEPOT_TOOLS_DIR:-${ROOT_DIR}/.depot_tools}"

log() {
  printf '\n==> %s\n' "$*"
}

configure_build_mode() {
  if [ -n "${BUILD_VARIANT:-}" ] || [ -n "${GN_ARGS:-}" ]; then
    BUILD_VARIANT="${BUILD_VARIANT:-host_release}"
    GN_ARGS="${GN_ARGS:---runtime-mode=release}"
    if [ -z "$JOBS" ] && [[ "$BUILD_VARIANT" == *release* ]]; then
      JOBS=2
    fi
    return
  fi

  case "$MODE" in
    release)
      BUILD_VARIANT="host_release"
      GN_ARGS="--runtime-mode=release"
      if [ "$ENABLE_LTO" != "1" ]; then
        GN_ARGS="${GN_ARGS} --no-lto"
      fi
      if [ -z "$JOBS" ]; then
        JOBS=2
      fi
      ;;
    profile)
      BUILD_VARIANT="host_profile"
      GN_ARGS="--runtime-mode=profile"
      ;;
    debug)
      BUILD_VARIANT="host_debug"
      GN_ARGS="--runtime-mode=debug"
      ;;
    debug_unopt)
      BUILD_VARIANT="host_debug_unopt"
      GN_ARGS="--unoptimized"
      ;;
    *)
      printf 'Unsupported MODE: %s\n' "$MODE" >&2
      printf 'Use one of: release, profile, debug, debug_unopt\n' >&2
      exit 2
      ;;
  esac
}

ensure_depot_tools() {
  if command -v gclient >/dev/null 2>&1; then
    return
  fi

  if [ ! -d "$DEPOT_TOOLS_DIR/.git" ]; then
    log "Installing depot_tools into ${DEPOT_TOOLS_DIR}"
    git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git "$DEPOT_TOOLS_DIR"
  fi

  export PATH="${DEPOT_TOOLS_DIR}:${PATH}"
}

ensure_checkout() {
  cd "$ROOT_DIR"

  if [ -z "$CHECKOUT_REF" ]; then
    log "Building current checkout: $(git rev-parse --abbrev-ref HEAD)"
    return
  fi

  if [ ! -d .git ]; then
    git init
  fi

  if ! git remote get-url origin >/dev/null 2>&1; then
    git remote add origin "$REMOTE_URL"
  fi

  log "Fetching ${CHECKOUT_REF}"
  git fetch origin "$CHECKOUT_REF"
  git checkout -B "$LOCAL_BRANCH" FETCH_HEAD
}

ensure_gclient_config() {
  cd "$ROOT_DIR"

  if [ ! -f .gclient ]; then
    log "Creating .gclient from engine/scripts/standard.gclient"
    cp engine/scripts/standard.gclient .gclient
  fi
}

sync_dependencies() {
  cd "$ROOT_DIR"

  if [ "$SKIP_SYNC" = "1" ]; then
    log "Skipping gclient sync because SKIP_SYNC=1"
    return
  fi

  log "Running gclient sync"
  gclient sync -D
}

build_engine() {
  cd "$ROOT_DIR/engine/src"

  log "Generating GN files with: flutter/tools/gn ${GN_ARGS}"
  flutter/tools/gn ${GN_ARGS}

  local ninja_args=(-C "out/${BUILD_VARIANT}")
  if [ -n "$JOBS" ]; then
    ninja_args+=("-j${JOBS}")
  fi
  if [ -n "$NINJA_TARGET" ]; then
    ninja_args+=("$NINJA_TARGET")
  fi

  log "Building with: ninja ${ninja_args[*]}"
  ninja "${ninja_args[@]}"
}

setup_flutter_tool_cache() {
  cd "$ROOT_DIR"

  bin/internal/update_engine_version.sh

  local cache_dir="${ROOT_DIR}/bin/cache"
  local out_dir="${ROOT_DIR}/engine/src/out/${BUILD_VARIANT}"
  local dart_sdk_dir="${ROOT_DIR}/engine/src/flutter/prebuilts/linux-x64/dart-sdk"
  local engine_version
  local framework_revision
  local framework_commit_date
  local framework_commit_epoch_ms
  local product_sdk_dir

  engine_version="$(< "${cache_dir}/engine.stamp")"
  framework_revision="$(git log -1 --format=%H HEAD)"
  framework_commit_date="$(git log -1 --format=%cI HEAD)"
  framework_commit_epoch_ms="$(git log -1 --format=%ct HEAD)000"
  product_sdk_dir="${out_dir}/flutter_patched_sdk_product"
  if [ ! -d "$product_sdk_dir" ]; then
    product_sdk_dir="${out_dir}/flutter_patched_sdk"
  fi

  log "Preparing local Flutter tool cache"
  mkdir -p "${cache_dir}/pkg" "${cache_dir}/artifacts/engine/common"

  rm -rf "${cache_dir}/dart-sdk" "${cache_dir}/dart-sdk-linux-x64.zip"

  ln -sfn "$dart_sdk_dir" "${cache_dir}/dart-sdk"
  ln -sfn "${out_dir}/gen/dart-pkg/sky_engine" "${cache_dir}/pkg/sky_engine"
  ln -sfn "${ROOT_DIR}/engine/src/flutter/lib/gpu" "${cache_dir}/pkg/flutter_gpu"
  ln -sfn "${out_dir}/flutter_patched_sdk" \
    "${cache_dir}/artifacts/engine/common/flutter_patched_sdk"
  ln -sfn "$product_sdk_dir" \
    "${cache_dir}/artifacts/engine/common/flutter_patched_sdk_product"
  ln -sfn "$out_dir" "${cache_dir}/artifacts/engine/linux-x64"
  ln -sfn "$out_dir" "${cache_dir}/artifacts/engine/linux-x64-profile"
  ln -sfn "$out_dir" "${cache_dir}/artifacts/engine/linux-x64-release"

  cp "${cache_dir}/engine.stamp" "${cache_dir}/engine-dart-sdk.stamp"
  cp "${cache_dir}/engine.stamp" "${cache_dir}/engine_stamp.stamp"
  cp "${cache_dir}/engine.stamp" "${cache_dir}/flutter_sdk.stamp"
  cp "${cache_dir}/engine.stamp" "${cache_dir}/font-subset.stamp"
  cp "${cache_dir}/engine.stamp" "${cache_dir}/linux-sdk.stamp"

  cat > "${cache_dir}/engine_stamp.json" <<EOF
{
  "build_time_ms": ${framework_commit_epoch_ms},
  "git_revision": "${engine_version}",
  "git_revision_date": "${framework_commit_date}",
  "content_hash": "local-skia-vulkan"
}
EOF

  if ! git rev-parse -q --verify "refs/tags/${LOCAL_FLUTTER_VERSION}" \
      >/dev/null; then
    git tag "$LOCAL_FLUTTER_VERSION" HEAD
  fi

  cat > "${cache_dir}/flutter.version.json" <<EOF
{
  "frameworkVersion": "${LOCAL_FLUTTER_VERSION}",
  "channel": "[user-branch]",
  "repositoryUrl": "unknown source",
  "frameworkRevision": "${framework_revision}",
  "frameworkCommitDate": "$(git log -1 --format=%ci HEAD)",
  "engineRevision": "${engine_version}",
  "engineCommitDate": "${framework_commit_date}",
  "engineContentHash": "local-skia-vulkan",
  "engineBuildDate": "${framework_commit_date}",
  "dartSdkVersion": "$("${dart_sdk_dir}/bin/dart" --version 2>&1 | sed 's/^Dart SDK version: //')",
  "devToolsVersion": "unknown",
  "flutterVersion": "${LOCAL_FLUTTER_VERSION}"
}
EOF
}

main() {
  configure_build_mode
  ensure_depot_tools
  ensure_checkout
  ensure_gclient_config
  sync_dependencies
  if [ "$CACHE_ONLY" != "1" ]; then
    build_engine
  else
    log "Skipping engine build because CACHE_ONLY=1"
  fi
  setup_flutter_tool_cache
}

main "$@"
