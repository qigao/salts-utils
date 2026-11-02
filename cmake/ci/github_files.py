#!/usr/bin/env python3
"""Download GitHub build artifacts through the existing curl transport."""
import argparse
import fnmatch
import hashlib
import os
from pathlib import Path
import re
import tempfile
import zipfile

from github_packages import Curl, archive_members


class Artifacts:
    def __init__(self, repository, token=None, api="https://api.github.com"):
        if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repository):
            raise ValueError("invalid GitHub repository")
        self.repository = repository
        self.base = f"{api}/repos/{repository}"
        self.curl = Curl(token=token, feed=api, bearer=True)

    def download(self, run_id, pattern, commit, destination):
        if not str(run_id).isdigit() or int(run_id) <= 0 or not re.fullmatch(r"[0-9a-f]{40}", commit):
            raise ValueError("an exact run ID and source commit are required")
        run = self.curl.json(f"{self.base}/actions/runs/{run_id}")
        if (run.get("head_sha") != commit
                or run.get("head_repository", {}).get("full_name") != self.repository):
            raise ValueError("artifact run repository/source commit mismatch")
        selected = []
        page = 1
        while True:
            values = self.curl.json(
                f"{self.base}/actions/runs/{run_id}/artifacts?per_page=100&page={page}")
            items = values["artifacts"]
            selected.extend(item for item in items if fnmatch.fnmatchcase(item["name"], pattern))
            if len(items) < 100:
                break
            page += 1
        if not selected or len({item["name"] for item in selected}) != len(selected):
            raise ValueError("missing or ambiguous build artifact")
        destination = Path(destination)
        if destination.exists() and any(destination.iterdir()):
            raise ValueError("artifact destination must be empty")
        destination.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=destination.parent) as temporary:
            temporary = Path(temporary)
            unpacked = temporary / "unpacked"
            unpacked.mkdir()
            seen = set()
            for item in sorted(selected, key=lambda value: value["name"]):
                digest = item.get("digest", "")
                if item.get("expired") or not re.fullmatch(r"sha256:[0-9a-f]{64}", digest):
                    raise ValueError("expired artifact or missing immutable digest")
                archive_path = temporary / "artifact.zip"
                artifact_id = int(item["id"])
                self.curl.download(f"{self.base}/actions/artifacts/{artifact_id}/zip", archive_path)
                with archive_path.open("rb") as stream:
                    actual = hashlib.file_digest(stream, "sha256").hexdigest()
                if "sha256:" + actual != digest:
                    raise ValueError("artifact SHA256 mismatch")
                with zipfile.ZipFile(archive_path) as archive:
                    entries = list(archive_members(archive))
                    for entry in entries:
                        key = entry.filename.casefold().rstrip("/")
                        if not entry.is_dir() and key in seen:
                            raise ValueError("overlapping build artifact payloads")
                        if not entry.is_dir():
                            seen.add(key)
                    for entry in entries:
                        archive.extract(entry, unpacked)
                        mode = (entry.external_attr >> 16) & 0o777
                        if mode and not entry.is_dir():
                            (unpacked / entry.filename).chmod(mode)
                print(f"curl restored artifact {item['name']} ({artifact_id})", flush=True)
            if destination.exists():
                destination.rmdir()
            unpacked.rename(destination)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    artifacts = commands.add_parser("artifacts")
    artifacts.add_argument("--repository", default=os.environ.get("GITHUB_REPOSITORY"))
    artifacts.add_argument("--run-id", default=os.environ.get("GITHUB_RUN_ID"))
    artifacts.add_argument("--pattern", required=True)
    artifacts.add_argument("--commit", required=True)
    artifacts.add_argument("--destination", type=Path, required=True)
    asset = commands.add_parser("asset")
    asset.add_argument("--url", required=True)
    asset.add_argument("--destination", type=Path, required=True)
    args = parser.parse_args()
    token = os.environ.get("GITHUB_TOKEN") or os.environ.get("GH_TOKEN")
    if args.command == "asset":
        if not re.fullmatch(r"https://api\.github\.com/repos/[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+/releases/assets/[0-9]+", args.url):
            raise ValueError("an authenticated GitHub release-asset API URL is required")
        Curl(token=token, feed="https://api.github.com", bearer=True).download(
            args.url, args.destination, headers=("Accept: application/octet-stream",))
    else:
        Artifacts(args.repository, token).download(args.run_id, args.pattern, args.commit, args.destination)


if __name__ == "__main__":
    main()
