#!/usr/bin/python3
import argparse
import glob
import json
import os
import re
import shutil
import subprocess
import sys

VULKAN_TOOLS_URL='https://github.com/LunarG/VulkanTools'

def build_known_good(args, sub_dir, previously_built = []):
  if args.verbose:
    print(sub_dir)
    print("Previously Built: " + ", ".join(previously_built))

  known_good = argparse.Namespace()
  repos = []

  fn = os.path.join(sub_dir, 'scripts', 'known_good.json')
  if not os.path.exists(fn):
    return

  with open(fn, 'r') as fh:
    known_good = argparse.Namespace(**json.load(fh))
    for repo in known_good.repos:
      if 'build_platforms' in repo:
        for build_platform in repo['build_platforms']:
          if build_platform == 'linux':
            repos.append(argparse.Namespace(**repo))
      elif repo['name'] != 'VulkanTools':
        repos.append(argparse.Namespace(**repo))

  if args.verbose:
    print("Repos:")
    for repo in repos:
      print("  " + repo.name)

  built = previously_built
  repos = [r for r in repos if r.name not in built]

  for repo in repos:
    if 'deps' not in repo:
      built.append(build_repo(args, repo, built))

  repos = [r for r in repos if r.name not in built]
  if args.verbose:
    print("Built: " + ", ".join(built))
    print("Repos:")
    for repo in repos:
      print("  " + repo.name)

  while repos:
    for repo in repos:
      ready = True
      for dep in repo.deps:
        if dep['repo_name'] not in built:
          ready = False

      if ready:
        built.append(build_repo(args, repo, built))

    repos = [r for r in repos if r.name not in built]
    if args.verbose:
      print("Built: " + ", ".join(built))
      print("Repos:")
      for repo in repos:
        print("  " + repo.name)

  return known_good

def build_repo(args, repo, built):
  print(repo.name)

  git_cmd = ['git', 'clone', repo.url, repo.sub_dir]
  if args.verbose:
    print(git_cmd)

  if subprocess.call(git_cmd) != 0:
    sys.exit(1)

  git_cmd = ['git', 'checkout', '--force', repo.commit]
  if args.verbose:
    print(git_cmd, repo.sub_dir)

  if subprocess.call(git_cmd, cwd=repo.sub_dir) != 0:
    sys.exit(1)

  if 'prebuild' in repo:
    for cmd in repo.prebuild:
      cmd = cmd.split()

      if args.verbose:
        print(cmd, repo.sub_dir)

      if subprocess.call(cmd, cwd=repo.sub_dir) != 0:
        sys.exit(1)

  build_known_good(args, repo.sub_dir, built)

  build_dir = os.path.join(repo.sub_dir, 'build')
  os.mkdir(build_dir)

  cmake_cmd = [args.cmake, '-G', args.generator, args.install_prefix, args.build_type]
  if 'cmake_options' in repo:
    cmake_cmd.extend(repo.cmake_options)

  # special case building the layers in Vulkan-ValidationLayers
  if repo.name == 'Vulkan-ValidationLayers':
    cmake_cmd = [opt for opt in cmake_cmd if opt != '-DBUILD_LAYERS=OFF']
    cmake_cmd.append('-DGLSLANG_INSTALL_DIR=' + args.install_prefix)

  if 'deps' in repo:
    for dep in repo.deps:
      cmake_cmd.append('-D' + dep['var_name'] + '=' + args.install_prefix)

  cmake_cmd.append('..')
  if args.verbose:
    print(cmake_cmd, build_dir)

  if subprocess.call(cmake_cmd, cwd=build_dir) != 0:
    sys.exit(1)

  cmake_cmd = [args.cmake, '--build', '.', '--target', 'install']
  if args.verbose:
    print(cmake_cmd, build_dir)

  if subprocess.call(cmake_cmd, cwd=build_dir) != 0:
    sys.exit(1)

  return repo.name

def merge_trees(src_dir, dst_dir):
  for name in os.listdir(src_dir):
    src_name = os.path.join(src_dir, name)
    if os.path.isdir(src_name):
      merge_trees(src_name, os.path.join(dst_dir, name))
    else:
      shutil.copy(src_name, dst_dir)

def build(args):
  if args.clean:
    if os.path.exists(args.install_dir):
      shutil.rmtree(args.install_dir)
    src_dirs = glob.glob('*')
    for src_dir in src_dirs:
      shutil.rmtree(src_dir)

  git_tag = '{}'.format(args.release)
  sub_dir = 'VulkanTools'

  git_cmd = ['git', 'clone', '-b', git_tag, VULKAN_TOOLS_URL, sub_dir]
  if args.verbose:
    print(git_cmd)

  if subprocess.call(git_cmd) != 0:
    sys.exit(1)

  if subprocess.call('./update_external_sources.sh', cwd=sub_dir) != 0:
    sys.exit(1)

  known_good = build_known_good(args, 'VulkanTools')

  build_dir = os.path.join(sub_dir, 'build')
  os.mkdir(build_dir)

  cmake_cmd = [args.cmake, '-G', args.generator, args.install_prefix, args.build_type]

  for var_name in known_good.install_names.values():
    cmake_cmd.append('-D' + var_name + '=' + args.install_dir)

  cmake_cmd.append('..')
  if args.verbose:
    print(cmake_cmd, build_dir)

  if subprocess.call(cmake_cmd, cwd=build_dir) != 0:
    sys.exit(1)

  cmake_cmd = [args.cmake, '--build', '.', '--target', 'install']
  if args.verbose:
    print(cmake_cmd, build_dir)

  if subprocess.call(cmake_cmd, cwd=build_dir) != 0:
    sys.exit(1)

  src_dir = os.path.join(args.install_dir, 'share', 'vulkan', 'explicit_layer.d')
  dst_dir = os.path.join(args.install_dir, 'etc', 'vulkan', 'explicit_layer.d')
  merge_trees(src_dir, dst_dir)
  shutil.rmtree(src_dir)

  src_dir = os.path.join(args.install_dir, 'lib64')
  if os.path.exists(src_dir):
    dst_dir = os.path.join(args.install_dir, 'lib')
    if os.path.exists(dst_dir):
      merge_trees(src_dir, dst_dir)
      shutil.rmtree(src_dir)
    else:
      shutil.move(src_dir, dst_dir)

  pkg_dir = os.path.join(args.install_dir, 'lib', 'pkgconfig')
  for name in os.listdir(pkg_dir):
    pkg_name = os.path.join(pkg_dir, name)
    print(pkg_name)
    if os.path.isfile(pkg_name):
      with open(pkg_name, 'r') as fh:
        lines = fh.readlines()

      with open(pkg_name, 'w') as fh:
        for line in lines:
          fh.write(re.sub(r'prefix\}/lib64$', r'prefix}/lib', line))


def parse_args():
  parser = argparse.ArgumentParser(description="Build the VulkanSDK")
  parser.add_argument('-v', '--verbose', action='store_true')
  parser.add_argument('--cmake', default="cmake3")
  parser.add_argument('-G', dest="generator", default="Ninja")
  parser.add_argument('--config', default="Release")
  parser.add_argument('--install_dir')
  parser.add_argument('--clean', action='store_true')
  parser.add_argument('release')

  args = parser.parse_args()
  if not args.install_dir:
    args.install_dir = os.path.abspath(os.path.join('..', args.release))

  args.build_type = '-DCMAKE_BUILD_TYPE=' + args.config
  args.install_prefix = '-DCMAKE_INSTALL_PREFIX=' + args.install_dir
  return args

if __name__ == "__main__":
  build(parse_args())
