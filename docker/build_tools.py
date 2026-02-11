#!/usr/bin/env python
from __future__ import annotations
from json import JSONDecodeError
import math
import pathlib
import time
import traceback
from typing import Callable, Dict
import sys
import os
import io
from os import walk

class Logger:
    NEWLINE_CHAR = '\n'
    with_crlf = False
    @classmethod
    def print_message(cls,message,prefix=''):
        if not Logger.with_crlf:
            trimmed=re.sub(r'\n', r'%0A', message,flags=re.MULTILINE)
        print(f'{prefix}{trimmed}')
    @classmethod
    def debug(cls,message):
        cls.print_message(message,'::debug::')


    @classmethod
    def error(cls,message):
        cls.print_message(message,'::error::')
    @classmethod
    def notice(cls,message):
        cls.print_message(message,'::notice::')    
    @classmethod
    def warning(cls,message):
        cls.print_message(message,'::notice::')   

try:

    import argparse
    import collections
    import copy
    import enum
    import glob

    import json
    import re
    import shutil
    import stat
    import tempfile
    import zipfile
    from ast import literal_eval
    from collections import namedtuple
    from datetime import datetime, timedelta, timezone
    from json import JSONDecoder
    from operator import contains
    from platform import platform, release
    from pydoc import describe
    from time import strftime
    from typing import OrderedDict
    from urllib import response
    from urllib.parse import urlparse
    from urllib.request import Request
    from webbrowser import get

    import pygit2
    from pygit2 import Commit, Repository, GitError, Reference, UserPass, Index, Signature, RemoteCallbacks, Remote
    import requests
    from genericpath import isdir

except ImportError as ex:
    OPTIONAL_IMPORT_ERROR = ex
    is_help_mode = "-h" in sys.argv[1:] or "--help" in sys.argv[1:]
    if not is_help_mode:
        Logger.error(
            f'Failed importing module {ex.name}, using interpreter {sys.executable}.')
        print(f'Environment: ')
        envlist = "\n".join([f"{k}={v}" for k, v in sorted(os.environ.items())])
        print(f'{envlist}')
else:
    OPTIONAL_IMPORT_ERROR = None

tool_version = "1.0.7"
WEB_INSTALLER_DEFAULT_PATH = './web_installer/'
FORMAT = '%(asctime)s %(message)s'

github_env = type('', (), {})()
manifest = {
    "name": "",
    "version": "",
    "home_assistant_domain": "slim_player",
    "funding_url": "https://esphome.io/guides/supporters.html",
    "new_install_prompt_erase": True,
    "new_install_improv_wait_time" : 20,
    "builds": [
        {
            "chipFamily": "ESP32",
            "parts": [
            ]
        }
    ]
}
artifacts_formats_outdir = '$OUTDIR'
artifacts_formats_prefix = '$PREFIX'
artifacts_formats = [
    ['build/squeezelite.bin', '$OUTDIR/$PREFIX-squeezelite.bin'],
    ['build/recovery.bin', '$OUTDIR/$PREFIX-recovery.bin'],
    ['build/ota_data_initial.bin', '$OUTDIR/$PREFIX-ota_data_initial.bin'],
    ['build/bootloader/bootloader.bin', '$OUTDIR/$PREFIX-bootloader.bin'],
    ['build/partition_table/partition-table.bin ',
        '$OUTDIR/$PREFIX-partition-table.bin'],
]


class AttributeDict(dict):
    __slots__ = ()

    def __getattr__(self, name: str):
        try:
            return self[name.upper()]
        except Exception:
            try:
                return self[name.lower()]
            except Exception:
                for attr in self.keys():
                    if name.lower() == attr.replace("'", "").lower():
                        return self[attr]
    __setattr__ = dict.__setitem__


parser = argparse.ArgumentParser(
    description='Handles some parts of the squeezelite-esp32 build process')
parser.add_argument('--cwd', type=str,
                    help='Working directory', default=os.getcwd())
parser.add_argument('--with_crlf', action='store_true',help='To prevent replacing cr/lf with hex representation')
parser.add_argument('--loglevel', type=str, choices={
                    'CRITICAL', 'ERROR', 'WARNING', 'INFO', 'DEBUG', 'NOTSET'}, help='Logging level', default='INFO')
subparsers = parser.add_subparsers(dest='command', required=True)

parser_commits = subparsers.add_parser("list_commits",add_help=False,
                                    description="Commits list",
                                    help="Lists the last commits"
                                    )
parser_changelog = subparsers.add_parser("changelog",add_help=False,
                                    description="Change Log",
                                    help="Shows the change log"
                                    )                                    

parser_dir = subparsers.add_parser("list_files",
                                   add_help=False,
                                   description="List Files parser",
                                   help="Display the content of the folder")

parser_manifest = subparsers.add_parser("manifest",
                                        add_help=False,
                                        description="Manifest parser",
                                        help="Handles the web installer manifest creation")
parser_manifest.add_argument('--flash_file', required=True, type=str,
                             help='The file path which contains the firmware flashing definition')
parser_manifest.add_argument(
    '--max_count', type=int, help='The maximum number of releases to keep', default=3)
parser_manifest.add_argument(
    '--manif_name', required=True, type=str, help='Manifest files name and prefix')
parser_manifest.add_argument(
    '--outdir', required=True, type=str, help='Output directory for files and manifests')


parser_pushinstaller = subparsers.add_parser("pushinstaller",
                                             add_help=False,
                                             description="Web Installer Checkout parser",
                                             help="Handles the creation of artifacts files")
parser_pushinstaller.add_argument(
    '--target', type=str, help='Output directory for web installer repository', default=WEB_INSTALLER_DEFAULT_PATH)
parser_pushinstaller.add_argument(
    '--artifacts', type=str, help='Target subdirectory for web installer artifacts', default=WEB_INSTALLER_DEFAULT_PATH)
parser_pushinstaller.add_argument(
    '--source', type=str, help='Source directory for the installer artifacts', default=WEB_INSTALLER_DEFAULT_PATH)
parser_pushinstaller.add_argument('--url', type=str, help='Web Installer clone url ',
                                  default='https://github.com/sle118/squeezelite-esp32-installer.git')
parser_pushinstaller.add_argument(
    '--web_installer_branch', type=str, help='Web Installer branch to use ', default='main')
parser_pushinstaller.add_argument(
    '--token', type=str, help='Auth token for pushing changes')
parser_pushinstaller.add_argument(
    '--flash_file', type=str, help='Manifest json file path')
parser_pushinstaller.add_argument(
    '--manif_name', required=True, type=str, help='Manifest files name and prefix')


parser_environment = subparsers.add_parser("environment",
                                           add_help=False,
                                           description="Environment parser",
                                           help="Updates the build environment")
parser_environment.add_argument(
    '--env_file', type=str, help='Environment File',  default=os.environ.get('GITHUB_ENV'))
parser_environment.add_argument(
    '--build', required=True, type=int, help='The build number')
parser_environment.add_argument(
    '--node', required=True, type=str, help='The matrix node being built')
parser_environment.add_argument(
    '--depth', required=True, type=int, help='The bit depth being built')
parser_environment.add_argument(
    '--major', type=str, help='Major version', default='2')
parser_environment.add_argument(
    '--docker', type=str, help='Docker image to use', default='espressif/idf:release-v5.5')

parser_show = subparsers.add_parser("show",
                                    add_help=False,
                                    description="Show parser",
                                    help="Show the build environment")
parser_build_flags = subparsers.add_parser("build_flags",
                                           add_help=False,
                                           description="Build Flags",
                                           help="Updates the build environment with build flags")
parser_build_flags.add_argument(
    '--mock', action='store_true', help='Mock release')
parser_build_flags.add_argument(
    '--force', action='store_true', help='Force a release build')
parser_build_flags.add_argument(
    '--ui_build', action='store_true', help='Include building the web UI')

def format_commit(commit):
    # 463a9d8b7 Merge branch 'bugfix/ci_deploy_tags_v4.0' into 'release/v4.0' (2020-01-11T14:08:55+08:00)
    dt = datetime.fromtimestamp(float(commit.author.time), timezone(
        timedelta(minutes=commit.author.offset)))
    #timestr = dt.strftime('%c%z')
    timestr = dt.strftime('%F %R %Z')
    cmesg:str = commit.message.replace('\n', ' ').replace('\r','').replace('*','-')
    return f'{commit.short_id} {cmesg} ({timestr}) <{commit.author.name}>'.replace('  ', ' ', )


def get_github_data(repo: Repository, api):
    base_url = urlparse(repo.remotes['origin'].url)
    print(
        f'Base URL is {base_url.path} from remote URL {repo.remotes["origin"].url}')
    url_parts = base_url.path.split('.')
    for p in url_parts:
        print(f'URL Part: {p}')
    api_url = f"{url_parts[0]}/{api}"
    print(f'API to call: {api_url}')
    url = f"https://api.github.com/repos{api_url}"
    resp = requests.get(
        url, headers={"Content-Type": "application/vnd.github.v3+json"})
    return json.loads(resp.text)


def dump_directory(dir_path):
    # list to store files name
    res = []
    for (dir_path, dir_names, file_names) in walk(dir_path):
        res.extend(file_names)
    print(res)


class ReleaseDetails():
    version: str
    idf: str
    platform: str
    branch: str
    bitrate: str

    def __init__(self, tag: str) -> None:
        self.version, self.idf, self.platform, self.branch = tag.split('#')
        try:
            self.version, self.bitrate = self.version.split('-')
        except Exception:
            pass

    def get_attributes(self):
        return {
            'version': self.version,
            'idf': self.idf,
            'platform': self.platform,
            'branch': self.branch,
            'bitrate': self.bitrate
        }

    def format_prefix(self) -> str:
        return f'{self.branch}-{self.platform}-{self.version}'

    def get_full_platform(self):
        return f"{self.platform}{f'-{self.bitrate}' if self.bitrate is not None else ''}"


class BinFile():
    name: str
    offset: int
    source_full_path: str
    target_name: str
    target_fullpath: str
    artifact_relpath: str

    def __init__(self, source_path, file_build_path: str, offset: int, release_details: ReleaseDetails, build_dir) -> None:
        self.name = os.path.basename(file_build_path).rstrip()
        self.artifact_relpath = os.path.relpath(
            file_build_path, build_dir).rstrip()
        self.source_path = source_path
        self.source_full_path = os.path.join(
            source_path, file_build_path).rstrip()
        self.offset = offset
        self.target_name = f'{release_details.format_prefix()}-{release_details.bitrate}-{self.name}'.rstrip()

    def get_manifest(self):
        return {"path": self.target_name, "offset": self.offset}

    def copy(self, target_folder) -> str:
        self.target_fullpath = os.path.join(target_folder, self.target_name)
        Logger.debug(
            f'File {self.source_full_path} will be copied to {self.target_fullpath}')
        try:
            os.makedirs(target_folder, exist_ok=True)
            shutil.copyfile(self.source_full_path,
                            self.target_fullpath, follow_symlinks=True)
        except Exception as ex:
            Logger.error(f"Error while copying {self.source_full_path} to {self.target_fullpath}{Logger.NEWLINE_CHAR}Content of {os.path.dirname(self.source_full_path.rstrip())}:{Logger.NEWLINE_CHAR}{Logger.NEWLINE_CHAR.join(get_file_list(os.path.dirname(self.source_full_path.rstrip())))}")
            
                
            
            raise
        return self.target_fullpath

    def get_attributes(self):
        return {
            'name': self.target_name,
            'offset': self.offset,
            'artifact_relpath': self.artifact_relpath
        }


class PlatformRelease():
    name: str
    description: str
    url: str = ''
    zipfile: str = ''
    tempfolder: str
    release_details: ReleaseDetails
    flash_parms = {}
    build_dir: str
    has_artifacts: bool
    branch: str
    assets: list
    bin_files: list
    name_prefix: str
    flash_file_path: str

    def get_manifest_name(self) -> str:
        return f'{self.name_prefix}-{self.release_details.format_prefix()}-{self.release_details.bitrate}.json'

    def __init__(self, flash_file_path, git_release, build_dir, branch, name_prefix) -> None:
        self.name = git_release.tag_name
        self.description = git_release.body
        self.assets = git_release['assets']
        self.has_artifacts = False
        self.name_prefix = name_prefix
        if len(self.assets) > 0:
            if self.has_asset_type():
                self.url = self.get_asset_from_extension().browser_download_url
            if self.has_asset_type('.zip'):
                self.zipfile = self.get_asset_from_extension(
                    ext='.zip').browser_download_url
                self.has_artifacts = True
        self.release_details = ReleaseDetails(git_release.name)
        self.bin_files = list()
        self.flash_file_path = flash_file_path
        self.build_dir = os.path.relpath(build_dir)
        self.branch = branch

    def process_files(self, outdir: str) -> list:
        parts = []
        for f in self.bin_files:
            f.copy(outdir)
            parts.append(f.get_manifest())
        return parts

    def get_asset_from_extension(self, ext='.bin'):
        for a in self.assets:
            filename = AttributeDict(a).name
            file_name, file_extension = os.path.splitext(filename)
            if file_extension == ext:
                return AttributeDict(a)
        return None

    def has_asset_type(self, ext='.bin') -> bool:
        return self.get_asset_from_extension(ext) is not None

    def platform(self):
        return self.release_details.get_full_platform()

    def get_zip_file(self):
        self.tempfolder = extract_files_from_archive(self.zipfile)
        print(
            f'Artifacts for {self.name} extracted to {self.tempfolder}')
        flash_parms_file = os.path.relpath(
            self.tempfolder+self.flash_file_path)
        line: str
        with open(flash_parms_file) as fin:
            for line in fin:
                components = line.split()
                if len(components) == 2:
                    self.flash_parms[os.path.basename(
                        components[1]).rstrip().lstrip()] = components[0]

        try:
            for artifact in artifacts_formats:
                base_name = os.path.basename(artifact[0]).rstrip().lstrip()
                self.bin_files.append(BinFile(
                    self.tempfolder, artifact[0], self.flash_parms[base_name], self.release_details, self.build_dir))
                has_artifacts = True
        except Exception:
            self.has_artifacts = False

    def cleanup(self):
        Logger.debug(f'removing temp directory for platform release {self.name}')
        shutil.rmtree(self.tempfolder)

    def get_attributes(self):
        return {
            'name': self.name,
            'branch': self.branch,
            'description': self.description,
            'url': self.url,
            'zipfile': self.zipfile,
            'release_details': self.release_details.get_attributes(),
            'bin_files': [b.get_attributes() for b in self.bin_files],
            'manifest_name': self.get_manifest_name()
        }


class Releases():
    _dict: dict = collections.OrderedDict()
    maxcount: int = 0
    branch: str = ''
    repo: Repository = None
    last_commit: Commit = None
    manifest_name: str

    def __init__(self, branch: str, maxcount: int = 3) -> None:
        self.maxcount = maxcount
        self.branch = branch

    def count(self, value: PlatformRelease) -> int:
        content = self._dict.get(value.platform())
        if content == None:
            return 0
        return len(content)

    def get_platform(self, platform: str) -> list:
        return self._dict[platform]

    def get_platform_keys(self):
        return self._dict.keys()

    def get_all(self) -> list:
        result: list = []
        for platform in [self.get_platform(platform) for platform in self.get_platform_keys()]:
            for release in platform:
                result.append(release)
        return result

    def append(self, value: PlatformRelease):
        if self.count(value) == 0:
            self._dict[value.platform()] = []
        if self.should_add(value):
            print(f'Adding release {value.name} to the list')
            self._dict[value.platform()].append(value)
        else:
            print(f'Skipping release {value.name}')

    def get_attributes(self):
        res = []
        release: PlatformRelease
        for release in self.get_all():
            res.append(release.get_attributes())
        return res

    def get_minlen(self) -> int:
        return min([len(self.get_platform(p)) for p in self.get_platform_keys()])

    def got_all_packages(self) -> bool:
        return self.get_minlen() >= self.maxcount

    def should_add(self, release: PlatformRelease) -> bool:
        return self.count(release) <= self.maxcount

    def add_package(self, package: PlatformRelease, with_artifacts: bool = True):
        if self.branch != package.branch:
            Logger.debug(f'Skipping release {package.name} from branch {package.branch}')
        elif package.has_artifacts or not with_artifacts:
            self.append(package)

    @classmethod
    def get_last_commit_message(cls, repo_obj: Repository = None) -> str:
        last: Commit = cls.get_last_commit(repo_obj)
        if last is None:
            return ''
        else:
            return last.message.replace(Logger.NEWLINE_CHAR, ' ')

    @classmethod
    def get_last_author(cls, repo_obj: Repository = None) -> Signature:
        last: Commit = cls.get_last_commit(repo_obj)
        return last.author

    @classmethod
    def get_last_committer(cls, repo_obj: Repository = None) -> Signature:
        last: Commit = cls.get_last_commit(repo_obj)
        return last.committer

    @classmethod
    def get_last_commit(cls, repo_obj: Repository = None) -> Commit:
        loc_repo = repo_obj
        if cls.repo is None:
            cls.load_repository(os.getcwd())
        if loc_repo is None:
            loc_repo = cls.repo

        head: Reference = loc_repo.head
        target = head.target
        ref: Reference
        if cls.last_commit is None:
            try:
                cls.last_commit = loc_repo[target]
                print(
                    f'Last commit for {head.shorthand} is {format_commit(cls.last_commit)}')
            except Exception as e:
                Logger.error(
                    f'Unable to retrieve last commit for {head.shorthand}/{target}: {e}')
                cls.last_commit = None
        return cls.last_commit

    @classmethod
    def load_repository(cls, path: str = os.getcwd()) -> Repository:
        if cls.repo is None:
            try:
                print(f'Opening repository from {path}')
                cls.repo = Repository(path=path)
            except GitError as ex:
                Logger.error(f"Unable to access the repository({ex}).\nContent of {path}:\n{Logger.NEWLINE_CHAR.join(get_file_list(path, 1))}")
                raise
        return cls.repo

    @classmethod
    def resolve_commit(cls, repo: Repository, commit_id: str) -> Commit:
        commit: Commit
        reference: Reference
        commit, reference = repo.resolve_refish(commit_id)
        return commit

    @classmethod
    def get_branch_name(cls) -> str:
        return re.sub(r'[^a-zA-Z0-9\-~!@_\.]', '', cls.load_repository().head.shorthand)

    @classmethod
    def get_release_branch(cls, repo: Repository, platform_release) -> str:
        match = [t for t in repo.branches.with_commit(
            platform_release.target_commitish)]
        no_origin = [t for t in match if 'origin' not in t]
        if len(no_origin) == 0 and len(match) > 0:
            return match[0].split('/')[1]
        elif len(no_origin) > 0:
            return no_origin[0]
        return ''

    @classmethod
    def get_flash_parms(cls, file_path):
        flash = parse_json(file_path)
        od: collections.OrderedDict = collections.OrderedDict()
        for z in flash['flash_files'].items():
            base_name: str = os.path.basename(z[1])
            od[base_name.rstrip().lstrip()] = literal_eval(z[0])
        return collections.OrderedDict(sorted(od.items()))

    @classmethod
    def get_releases(cls, flash_file_path, maxcount: int, name_prefix):
        repo = Releases.load_repository(os.getcwd())
        packages: Releases = cls(branch=repo.head.shorthand, maxcount=maxcount)
        build_dir = os.path.dirname(flash_file_path)
        for page in range(1, 999):
            Logger.debug(f'Getting releases page {page}')
            releases = get_github_data(
                repo, f'releases?per_page=50&page={page}')
            if len(releases) == 0:
                Logger.debug(f'No more release found for page {page}')
                break
            for release_entry in [AttributeDict(platform) for platform in releases]:
                packages.add_package(PlatformRelease(flash_file_path, release_entry, build_dir,
                                     Releases.get_release_branch(repo, release_entry), name_prefix))
                if packages.got_all_packages():
                    break
            if packages.got_all_packages():
                break

        return packages

    @classmethod
    def get_commit_list(cls) -> list:
        commit_list = []
        last: Commit = Releases.get_last_commit()
        if last is None:
            return commit_list
        try:
            for c in Releases.load_repository().walk(last.id, pygit2.GIT_SORT_TIME):
                if '[skip actions]' not in c.message:
                    commit_list.append(format_commit(c))
                    if len(commit_list) > 10:
                        break

        except Exception as e:
            Logger.error(
                f'Unable to get commit list starting at {last.id}: {e}')

        return commit_list

    @classmethod
    def get_commit_list_descriptions(cls) -> str:
        # return '<<~EOD\n### Revision Log\n'+Logger.NEWLINE_CHAR.join(cls.get_commit_list())+'\n~EOD'
        return '<<~EOD\n### Revision Log\n'+Logger.NEWLINE_CHAR.join(cls.get_commit_list())+'\n~EOD'
    @classmethod
    def get_changelog(cls) -> str:
        # return '<<~EOD\n### Revision Log\n'+Logger.NEWLINE_CHAR.join(cls.get_commit_list())+'\n~EOD'
        fname = os.path.abspath('CHANGELOG')
        folder: str = os.path.abspath(os.path.dirname(fname))
        print(f'Opening changelog file {fname} from {folder}')
        try:
            with open(fname) as f:
                content = f.read()
                Logger.debug(f'Change Log:\n{content}')
                return f'<<~EOD\n{content}\n~EOD'
        except Exception as ex:
            Logger.error(
                f"Unable to load change log file content. Content of {folder}:{Logger.NEWLINE_CHAR.join(get_file_list(folder))}")
            raise

        return f'<<~EOD\n### Revision Log\n\n~EOD'

    def update(self, *args, **kwargs):
        if args:
            if len(args) > 1:
                raise TypeError("update expected at most 1 arguments, "
                                "got %d" % len(args))
            other = dict(args[0])
            for key in other:
                self[key] = other[key]
        for key in kwargs:
            self[key] = kwargs[key]

    def setdefault(self, key, value=None):
        if key not in self:
            self[key] = value
        return self[key]


def set_workdir(args):
    print(f'setting work dir to: {args.cwd}')
    os.chdir(os.path.abspath(args.cwd))


def parse_json(filename: str):
    fname = os.path.abspath(filename)
    folder: str = os.path.abspath(os.path.dirname(filename))
    print(f'Opening json file {fname} from {folder}')
    try:
        with open(fname) as f:
            content = f.read()
            Logger.debug(f'Loading json\n{content}')
            return json.loads(content)
    except JSONDecodeError as ex:
        Logger.error(f'Error parsing {content}')
    except Exception as ex:
        Logger.error(
            f"Unable to parse flasher args json file. Content of {folder}:{Logger.NEWLINE_CHAR.join(get_file_list(folder))}")
        raise


def write_github_env_file(values,env_file):
    env_file_stream = None
    if env_file is not None:
        print(f'Writing content to {env_file}...')
        env_file_stream = open(env_file,  "w")
    else:
        print(f'Writing content to console...')
        env_file_stream = sys.stdout
    for attr in [attr for attr in dir(values) if not attr.startswith('_')]:
        line = f'{attr}{"=" if attr != "description" else ""}{getattr(values,attr)}'
        if env_file is not None:
            print(line)
        env_file_stream.write(f'{line}\n')
        os.environ[attr] = str(getattr(values, attr))
    if env_file is not None:
        print(f'Done writing to {env_file}!')
        env_file_stream.close()
    else:
        print(f'Done Writing content to console...')



def format_artifact_from_manifest(manif_json: AttributeDict):
    if len(manif_json) == 0:
        return 'Newest release'
    first = manif_json[0]
    return f'{first["branch"]}-{first["release_details"]["version"]}'


def format_artifact_name(base_name: str = '', args=AttributeDict(os.environ)):
    return f'{base_name}{args.branch_name}-{args.node}-{args.depth}-{args.major}{args.build}'

def handle_build_flags(args):
    set_workdir(args)
    print('Setting global build flags')
    commit_message: str = Releases.get_last_commit_message()
    github_env.mock = 1 if args.mock else 0
    github_env.release_flag = 1 if args.mock or args.force or 'release' in commit_message.lower() else 0
    github_env.ui_build = 1 if args.mock or args.ui_build or '[ui-build]' in commit_message.lower()  else 0
    write_github_env_file(github_env,os.environ.get('GITHUB_OUTPUT'))

def write_version_number(file_path:str,env_details):
    #     app_name="${TARGET_BUILD_NAME}.${DEPTH}.dev-$(git log --pretty=format:'%h' --max-count=1).${branch_name}" 
    #     echo "${app_name}">version.txt
    try:
        version:str = f'{env_details.TARGET_BUILD_NAME}.{env_details.DEPTH}.{env_details.major}.{env_details.BUILD_NUMBER}.{env_details.branch_name}'
        with open(file_path,  "w") as version_file:
            version_file.write(version)
    except Exception as ex:
        Logger.error(f'Unable to set version string {version} in file {file_path}')
        raise Exception('Version error')
    Logger.notice(f'Firmware version set to {version}')


def handle_environment(args):
    set_workdir(args)
    print('Setting environment variables...')
    commit_message: str = Releases.get_last_commit_message()
    last: Commit = Releases.get_last_commit()
    if last is not None:
        github_env.author_name = last.author.name
        github_env.author_email = last.author.email
        github_env.committer_name = last.committer.name
        github_env.committer_email = last.committer.email
    github_env.node = args.node
    github_env.depth = args.depth
    github_env.major = args.major
    github_env.build = args.build
    github_env.DEPTH = args.depth
    github_env.TARGET_BUILD_NAME = args.node
    github_env.build_version_prefix = args.major
    github_env.branch_name = Releases.get_branch_name()
    github_env.BUILD_NUMBER = str(args.build)
    github_env.tag = f'{args.node}.{args.depth}.{args.build}.{github_env.branch_name}'.rstrip()
    github_env.last_commit = commit_message
    github_env.DOCKER_IMAGE_NAME = args.docker
    github_env.name = f"{args.major}.{str(args.build)}-{args.depth}#v5.5#{args.node}#{github_env.branch_name}"
    github_env.artifact_prefix = format_artifact_name(
        'squeezelite-esp32-', github_env)
    github_env.artifact_file_name = f"{github_env.artifact_prefix}.zip"
    github_env.artifact_bin_file_name = f"{github_env.artifact_prefix}.bin"
    github_env.PROJECT_VER = f'{args.node}-{ args.build }'
    github_env.description = Releases.get_changelog()
    write_github_env_file(github_env,args.env_file)
    write_version_number("version.txt",github_env)

def handle_artifacts(args):
    set_workdir(args)
    print(f'Handling artifacts')
    for attr in artifacts_formats:
        target: str = os.path.relpath(attr[1].replace(artifacts_formats_outdir, args.outdir).replace(
            artifacts_formats_prefix, format_artifact_name()))
        source: str = os.path.relpath(attr[0])
        target_dir: str = os.path.dirname(target)
        print(f'Copying file {source} to {target}')
        try:
            os.makedirs(target_dir, exist_ok=True)
            shutil.copyfile(source, target, follow_symlinks=True)
        except Exception as ex:
            Logger.error(f"Error while copying {source} to {target}\nContent of {target_dir}:\n{Logger.NEWLINE_CHAR.join(get_file_list(os.path.dirname(attr[0].rstrip())))}")
            raise


def delete_folder(path):
    '''Remov Read Only Files'''
    for root, dirs, files in os.walk(path, topdown=True):
        for dir in dirs:
            fulldirpath = os.path.join(root, dir)
            Logger.debug(f'Drilling down in {fulldirpath}')
            delete_folder(fulldirpath)
        for fname in files:
            full_path = os.path.join(root, fname)
            Logger.debug(f'Setting file read/write {full_path}')
            os.chmod(full_path, stat.S_IWRITE)
            Logger.debug(f'Deleting file {full_path}')
            os.remove(full_path)
    if os.path.exists(path):
        Logger.debug(f'Changing folder read/write {path}')
        os.chmod(path, stat.S_IWRITE)
        print(f'WARNING: Deleting Folder {path}')
        os.rmdir(path)


def get_file_stats(path):
    fstat: os.stat_result = pathlib.Path(path).stat()
    # Convert file size to MB, KB or Bytes
    mtime = time.strftime("%X %x", time.gmtime(fstat.st_mtime))
    if (fstat.st_size > 1024 * 1024):
        return math.ceil(fstat.st_size / (1024 * 1024)), "MB", mtime
    elif (fstat.st_size > 1024):
        return math.ceil(fstat.st_size / 1024), "KB", mtime
    return fstat.st_size, "B", mtime


def get_file_list(root_path, max_levels: int = 2) -> list:
    outlist: list = []
    for root, dirs, files in os.walk(root_path):
        path = os.path.relpath(root).split(os.sep)
        if len(path) <= max_levels:
            outlist.append(f'\n{root}')
            for file in files:
                full_name = os.path.join(root, file)
                fsize, unit, mtime = get_file_stats(full_name)
                outlist.append('{:s} {:8d} {:2s} {:18s}\t{:s}'.format(
                    len(path) * "---", fsize, unit, mtime, file))
    return outlist


def get_recursive_list(path) -> list:
    outlist: list = []
    for root, dirs, files in os.walk(path, topdown=True):
        for fname in files:
            outlist.append((fname, os.path.join(root, fname)))
    return outlist


def handle_manifest(args):
    set_workdir(args)
    print(f'Creating the web installer manifest')
    outdir: str = os.path.relpath(args.outdir)
    if not os.path.exists(outdir):
        print(f'Creating target folder {outdir}')
        os.makedirs(outdir, exist_ok=True)
    releases: Releases = Releases.get_releases(
        args.flash_file, args.max_count, args.manif_name)
    release: PlatformRelease
    for release in releases.get_all():
        manifest_name = release.get_manifest_name()
        release.get_zip_file()
        man = copy.deepcopy(manifest)
        man['manifest_name'] = manifest_name
        man['builds'][0]['parts'] = release.process_files(args.outdir)
        man['name'] = release.platform()
        man['version'] = release.release_details.version
        Logger.debug(f'Generated manifest: \n{json.dumps(man)}')
        fullpath = os.path.join(args.outdir, release.get_manifest_name())
        print(f'Writing manifest to {fullpath}')
        with open(fullpath, "w") as f:
            json.dump(man, f, indent=4)
        release.cleanup()
    mainmanifest = os.path.join(args.outdir, args.manif_name)
    print(f'Writing main manifest {mainmanifest}')
    with open(mainmanifest, 'w') as f:
        json.dump(releases.get_attributes(), f, indent=4)


def get_new_file_names(manif_json) -> collections.OrderedDict():
    new_release_files: dict = collections.OrderedDict()
    for artifact in manif_json:
        for name in [f["name"] for f in artifact["bin_files"]]:
            new_release_files[name] = artifact
        new_release_files[artifact["manifest_name"]] = artifact["name"]
    return new_release_files


def copy_no_overwrite(source: str, target: str):
    sfiles = os.listdir(source)
    for f in sfiles:
        source_file = os.path.join(source, f)
        target_file = os.path.join(target, f)
        if not os.path.exists(target_file):
            print(f'Copying {f} to target')
            shutil.copy(source_file, target_file)
        else:
            Logger.debug(f'Skipping existing file {f}')


def get_changed_items(repo: Repository) -> Dict:
    changed_filemode_status_code: int = pygit2.GIT_FILEMODE_TREE
    original_status_dict: Dict[str, int] = repo.status()
    # transfer any non-filemode changes to a new dictionary
    status_dict: Dict[str, int] = {}
    for filename, code in original_status_dict.items():
        if code != changed_filemode_status_code:
            status_dict[filename] = code
    return status_dict


def is_dirty(repo: Repository) -> bool:
    return len(get_changed_items(repo)) > 0

def push_with_method(auth_method:str,token:str,remote: Remote,reference):
    success:bool = False
    try:
        remote.push(reference, callbacks=RemoteCallbacks(pygit2.UserPass(auth_method, token)))
        success=True
    except Exception as ex:
        Logger.error(f'Error pushing with auth method {auth_method}: {ex}.')
    return success

def push_if_change(repo: Repository, token: str, source_path: str, manif_json):
    if is_dirty(repo):
        print(f'Changes found. Preparing commit')
        env = AttributeDict(os.environ)
        index: Index = repo.index
        index.add_all()
        index.write()
        reference = repo.head.name
        message = f'Web installer for {format_artifact_from_manifest(manif_json)}'
        tree = index.write_tree()
        Releases.load_repository(source_path)
        commit = repo.create_commit(reference, Releases.get_last_author(
        ), Releases.get_last_committer(), message, tree, [repo.head.target])
        origin: Remote = repo.remotes['origin']
        print(
            f'Pushing commit {format_commit(repo[commit])} to url {origin.url}')
        remote: Remote = repo.remotes['origin']
        auth_methods = ['x-access-token','x-oauth-basic']
        for method in auth_methods:
            if push_with_method(method, token, remote, [reference]):
                print(f'::notice Web installer updated for {format_artifact_from_manifest(manif_json)}')
                return
        raise Exception('Unable to push web installer.')    

    else:
        print(f'WARNING: No change found. Skipping update')


def update_files(target_artifacts: str, manif_json, source: str):
    new_list: dict = get_new_file_names(manif_json)
    if os.path.exists(target_artifacts):
        print(f'Removing obsolete files from {target_artifacts}')
        for entry in get_recursive_list(target_artifacts):
            f = entry[0]
            full_target = entry[1]
            if f not in new_list.keys():
                print(f'WARNING: Removing obsolete file {f}')
                os.remove(full_target)
    else:
        print(f'Creating target folder {target_artifacts}')
        os.makedirs(target_artifacts, exist_ok=True)
    print(f'Copying installer files to {target_artifacts}:')
    copy_no_overwrite(os.path.abspath(source), target_artifacts)


def handle_pushinstaller(args):
    set_workdir(args)
    print('Pushing web installer updates... ')
    target_artifacts = os.path.join(args.target, args.artifacts)
    if os.path.exists(args.target):
        print(f'Removing files (if any) from {args.target}')
        delete_folder(args.target)
    print(f'Cloning from {args.url} into {args.target}')
    repo = pygit2.clone_repository(args.url, args.target)
    repo.checkout_head()
    manif_json = parse_json(os.path.join(args.source, args.manif_name))
    update_files(target_artifacts, manif_json, args.source)
    push_if_change(repo, args.token, args.cwd, manif_json)
    repo.state_cleanup()


def handle_show(args):
    print('Show')


def extract_files_from_archive(url):
    tempfolder = tempfile.mkdtemp()
    platform = requests.get(url)
    Logger.debug(f'Downloading {url} to {tempfolder}')
    Logger.debug(f'Transfer status code: {platform.status_code}. Expanding content')
    z = zipfile.ZipFile(io.BytesIO(platform.content))
    z.extractall(tempfolder)
    return tempfolder


def handle_list_files(args):
    print(f'Content of {args.cwd}:')
    print(Logger.NEWLINE_CHAR.join(get_file_list(args.cwd)))

def handle_commits(args):
    set_workdir(args)
    print(Releases.get_commit_list_descriptions())
def handle_changelog(args):
    set_workdir(args)
    print(Releases.get_changelog())


parser_environment.set_defaults(func=handle_environment, cmd='environment')
parser_manifest.set_defaults(func=handle_manifest, cmd='manifest')
parser_pushinstaller.set_defaults(func=handle_pushinstaller, cmd='installer')
parser_show.set_defaults(func=handle_show, cmd='show')
parser_build_flags.set_defaults(func=handle_build_flags, cmd='build_flags')
parser_dir.set_defaults(func=handle_list_files, cmd='list_files')
parser_commits.set_defaults(func=handle_commits,cmd='list_commits')
parser_changelog.set_defaults(func=handle_changelog,cmd='changelog')

def main():
    exit_result_code = 0
    args = parser.parse_args()
    Logger.with_crlf = args.with_crlf
    print(f'::group::{args.command}')
    print(f'build_tools version : {tool_version}')
    print(f'Processing command {args.command}')
    func: Callable = getattr(args, 'func', None)
    if func is not None:
        if OPTIONAL_IMPORT_ERROR is not None and args.command not in {'show', 'list_files'}:
            Logger.error(f"Required dependency '{OPTIONAL_IMPORT_ERROR.name}' is missing. "
                         "Install dependencies first: python -m pip install setuptools pygit2 requests protobuf grpcio-tools")
            print(f'::endgroup::')
            sys.exit(1)
        # Call whatever subcommand function was selected
        
        e: Exception
        try:
            func(args)
        except Exception as e:
            Logger.error(f'Critical error while running {args.command}\n{" ".join(traceback.format_exception(etype=type(e), value=e, tb=e.__traceback__))}')
            exit_result_code = 1
    else:
        # No subcommand was provided, so call help
        parser.print_usage()
    print(f'::endgroup::')
    sys.exit(exit_result_code)


if __name__ == '__main__':
    main()
