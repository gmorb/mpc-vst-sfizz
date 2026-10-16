# Release sfizz for MPC OS 1.0.4: instructions for an agent

These instructions publish **sfizz for MPC OS 1.0.4** on GitHub as `gmorb/mpc-vst-sfizz` and list it in the
[MPC OS plugin catalog](https://github.com/sd88me/mpc-vst-plugins) by sd88me. Follow them in order. Each step has a
**check**: run it and show its real output before you call the step done.

## Rules
- **Never report a GitHub state you didn't just see in command output.** "Pushed", "created", "uploaded", "published",
  "PR opened" each need the command that did it and the command that checked it. Every URL you report comes from
  command output (`--json url`). If a step fails or you can't run it, report "not done" and why. No placeholders.
- **Upload the delivered files exactly as they are.** Don't rebuild or re-zip them: the checksums below are the proof
  that nothing changed between the files the owner tests and the files the catalog serves.
- Never force-push, delete or move a tag, rewrite history, or create a second release for a tag. If something already
  exists and differs from what is described here, **stop and ask**.
- Use `gh` for everything it can do. Only the repository's social preview image needs the browser (step 4).
- There are two hard stops: step 8 (owner tests the draft) and the end of step 10 (the owner gives the device facts).
  Don't continue past a stop without the owner's go.

## Fixed facts (don't "correct" them)
| | |
|---|---|
| GitHub repo | `gmorb/mpc-vst-sfizz`, public, default branch `main` |
| Tag / release title | `v1.0.4` / `sfizz for MPC OS 1.0.4` |
| Plugin | name `sfizz`, vendor `sfztools`, uid `SfzM`, catalog id `sfizz`, licence `BSD-2-Clause` |
| Gen1 asset | `sfizz-1.0.4-mpc-armv7.zip`, sha256 `c3a1e88c43d0b531bcb4ea2b1a965acce090fdf574e606c5a4bf4fed65847569` |
| Gen2 asset | `sfizz-1.0.4-mpc-aarch64.zip`, sha256 `39231a313ed8af9703ed650e8a196ccda46a2bf578cfd33128f9d964caa888da` |
| Standalone asset | `sfizz-for-MPC-OS-1.0.4.zip` (Gen1 plugin folder for `vstscanner.sh`), sha256 `6db86546d7c4b21f076de012fed8c3090482c928cdc487a79e82e3b9e6f71cf4` |
| Other assets | `SHA256SUMS`, `sfizz-for-MPC-OS-1.0.4-manual.pdf` (its sha256 is in `SHA256SUMS`) |
| Release notes | `packaging/release-notes-v1.0.4.md` (in the repo) |
| Catalog entry | `packaging/catalog/sfizz.json` → `catalog/plugins/sfizz.json` in sd88me/mpc-vst-plugins |
| Kit commit for checks | `e18a0a1da84515096d354f399dd4cd7f9c079fb5` (sd88me/mpc-vst-plugins) |

One release carries both zips. The catalog has **one** entry for the plugin; it finds the Gen2 zip itself by swapping
`armv7` for `aarch64` in the entry's `asset_pattern`. Never add a second entry for Gen2. The standalone zip is for people
who install by hand with `vstscanner.sh`; the catalog doesn't use it.

## 0. Inputs
The owner gives you these files. Put them in one working folder:
- `mpc-vst-sfizz-repo-1.0.4-with-git.tar.gz`: the repository with its full history
- `sfizz-1.0.4-mpc-armv7.zip`, `sfizz-1.0.4-mpc-aarch64.zip`, `sfizz-for-MPC-OS-1.0.4.zip`, `SHA256SUMS`
- `sfizz-for-MPC-OS-1.0.4-manual.pdf`

```
mkdir -p rel && mv sfizz-1.0.4-mpc-*.zip sfizz-for-MPC-OS-1.0.4.zip SHA256SUMS sfizz-for-MPC-OS-1.0.4-manual.pdf rel/
(cd rel && sha256sum -c SHA256SUMS)                       # check: all four files "OK"
tar -xzf mpc-vst-sfizz-repo-1.0.4-with-git.tar.gz         # unpacks mpc-vst-sfizz/
git -C mpc-vst-sfizz status --short | head                # check: empty (clean tree)
git -C mpc-vst-sfizz log --oneline -3                     # note the HEAD commit
```
If a checksum fails or the tree isn't clean, stop and ask.

## 1. Preflight
```
gh auth status                                            # check: logged in as gmorb, scopes include repo
gh api user --jq .login                                   # check: gmorb
```
If either fails, stop: nothing below works without it.

## 2. State: what exists right now
```
R=gmorb/mpc-vst-sfizz
gh repo view $R --json url,visibility,defaultBranchRef 2>&1 | head -5
git ls-remote --heads --tags https://github.com/$R 2>&1 | head
gh release list --repo $R 2>&1 | head
gh pr list --repo sd88me/mpc-vst-plugins --author @me --state all --json number,title,state,url
```
- Repo not found → step 3a. Repo exists and is empty → step 3b. Repo exists with commits → compare its `main` with the
  tarball's history (`git -C mpc-vst-sfizz fetch https://github.com/$R main && git -C mpc-vst-sfizz merge-base --is-ancestor FETCH_HEAD HEAD`).
  If GitHub's `main` is an ancestor of the tarball's HEAD, step 3b pushes the new commits on top; otherwise stop and ask.
- A `v1.0.4` tag or release that already exists: stop and ask (don't make a second one).
- An open catalog PR for sfizz: don't open another; report it.

## 3. Push the repository
**3a. Create it** (only if it doesn't exist):
```
cd mpc-vst-sfizz
gh repo create gmorb/mpc-vst-sfizz --public --source . --remote origin \
  --description "sfizz 1.2.3 SFZ sampler for MPC OS standalone hardware (Gen1 armv7 + Gen2 aarch64), with a one-page screen modelled on sfizz's editor"
```
**3b. Push `main`:**
```
git remote get-url origin 2>/dev/null || git remote add origin https://github.com/gmorb/mpc-vst-sfizz.git
git push -u origin main
git ls-remote https://github.com/gmorb/mpc-vst-sfizz main ; git rev-parse HEAD     # check: same commit
```

## 4. Make the repo page look finished
```
gh repo edit gmorb/mpc-vst-sfizz \
  --description "sfizz 1.2.3 SFZ sampler for MPC OS standalone hardware (Gen1 armv7 + Gen2 aarch64), with a one-page screen modelled on sfizz's editor" \
  --add-topic mpc --add-topic mpc-os --add-topic akai-mpc --add-topic sfz --add-topic sfizz --add-topic sampler \
  --add-topic vst --add-topic arm --add-topic aarch64 --enable-issues --enable-wiki=false
gh repo view gmorb/mpc-vst-sfizz --json description,repositoryTopics,hasIssuesEnabled     # check
```
The README shows `docs/images/banner.png`, the screen, the Gen1/Gen2 table and the CPU chart; check that they render:
`gh api repos/gmorb/mpc-vst-sfizz/contents/docs/images --jq '.[].name'` (expect `banner.png`, `cpu.png`).

**Social preview (browser, owner):** tell the owner to upload `docs/images/banner.png` (1280×640) under the repo's
Settings › General › Social preview. `gh` can't do this; say it is left for the owner and don't claim it as done.

## 5. Tag
```
git tag -a v1.0.4 -m "sfizz for MPC OS 1.0.4" && git push origin v1.0.4
git ls-remote --tags https://github.com/gmorb/mpc-vst-sfizz v1.0.4                  # check: the tag, at HEAD
```

## 6. Draft release with the five assets
```
gh release create v1.0.4 --repo gmorb/mpc-vst-sfizz --draft --verify-tag \
  --title "sfizz for MPC OS 1.0.4" --notes-file packaging/release-notes-v1.0.4.md \
  ../rel/sfizz-1.0.4-mpc-armv7.zip ../rel/sfizz-1.0.4-mpc-aarch64.zip ../rel/sfizz-for-MPC-OS-1.0.4.zip \
  ../rel/sfizz-for-MPC-OS-1.0.4-manual.pdf ../rel/SHA256SUMS
gh release view v1.0.4 --repo gmorb/mpc-vst-sfizz --json tagName,name,isDraft,isPrerelease,url,assets \
  --jq '{tag:.tagName,name:.name,draft:.isDraft,pre:.isPrerelease,url:.url,assets:[.assets[]|{name,size}]}'
```
Check: `draft: true`, `pre: false`, five assets with these sizes: armv7 zip 1447306 bytes, aarch64 zip 1525219 bytes,
standalone zip 1455717 bytes, manual 664507 bytes, `SHA256SUMS` 379 bytes.

## 7. Check the uploaded files the way the catalog will
Scratch folders (`dl`, `kit`, `reg`, `catout`) go beside the repo, never inside it.
```
rm -rf ../dl && gh release download v1.0.4 --repo gmorb/mpc-vst-sfizz -D ../dl
(cd ../dl && sha256sum -c SHA256SUMS)                                               # check: all four OK
git clone -q https://github.com/sd88me/mpc-vst-plugins ../kit && git -C ../kit checkout -q e18a0a1da84515096d354f399dd4cd7f9c079fb5
for z in ../dl/sfizz-1.0.4-mpc-armv7.zip ../dl/sfizz-1.0.4-mpc-aarch64.zip; do
  python3 ../kit/tools/catalog_check.py $z --catalog --expect-id sfizz --expect-repo gmorb/mpc-vst-sfizz; done
unzip -l ../dl/sfizz-for-MPC-OS-1.0.4.zip | grep -cE "sfizz for MPC OS/(README.md|sfztools - VST - sfizz/(plugin-meta.xml|sfizz_mpc.so))$"
```
Check: each catalog check ends `OK: sfizz 1.0.4 (0 warnings)`, and the last line prints `3` (the standalone zip has its
README, `plugin-meta.xml` and `.so`).

## 8. STOP: report and wait
Report: the state output (step 2), the repo URL, the draft release URL, the assets with sizes, the checksum and catalog
check output, and the social-preview note. Then wait. The owner tests the draft's zips on their devices and says **go**
(and whether the Gen2 zip was tested on a Gen2 device). This build changed the screen's drop-down lists and the SFZ browser
(six larger rows) since the last device test, so remind the owner to open a few lists (Sample quality, the tuning
presets) and to browse a folder with more than six files, paging down and up, on the device.

If the owner says the Gen2 zip *was* tested on a Gen2 device before you publish, update the notes' last Gen2 sentence
("tested offline (emulated) only so far") to name the device, in `packaging/release-notes-v1.0.4.md` on `main` (commit,
push), then `gh release edit v1.0.4 --repo gmorb/mpc-vst-sfizz --notes-file packaging/release-notes-v1.0.4.md` and check
with `gh release view v1.0.4 --repo gmorb/mpc-vst-sfizz --json body --jq .body | grep -n Gen2`. The tag stays where it is.

## 9. Publish (only after go)
```
gh release edit v1.0.4 --repo gmorb/mpc-vst-sfizz --draft=false --latest
gh release view v1.0.4 --repo gmorb/mpc-vst-sfizz --json isDraft,url,publishedAt                # check: isDraft false
curl -sI https://github.com/gmorb/mpc-vst-sfizz/releases/download/v1.0.4/sfizz-1.0.4-mpc-aarch64.zip | head -1
curl -sI https://github.com/gmorb/mpc-vst-sfizz/releases/download/v1.0.4/sfizz-1.0.4-mpc-armv7.zip | head -1
```
Check: both `curl` lines are `HTTP/2 302` (a public download, no login).

## 10. Record the hardware tests (`tested.json`)
This is what gives the catalog's **Verified Gen1** / **Verified Gen2** badges. Ask the owner, for each device they
tested 1.0.4 on: the device model, its MPC OS version and the date. **Never guess or fill these in yourself.** A Gen2
test needs "Gen2" in `device`. Example (replace with the owner's real answers; leave out any line they didn't test):
```json
[
  { "version": "1.0.4", "device": "MPC Live II", "firmware": "3.6.0", "date": "2026-10-16" },
  { "version": "1.0.4", "device": "MPC Live III (Gen2)", "firmware": "5.0.18", "date": "2026-10-16" }
]
```
Put it at the repo root on `main`, then:
```
python3 -m json.tool tested.json >/dev/null && git add tested.json && git commit -m "tested.json: 1.0.4 device tests" && git push origin main
gh api repos/gmorb/mpc-vst-sfizz/contents/tested.json --jq .name                    # check: tested.json
```
If the owner tested nothing yet, skip this step and say so; the plugin is then listed without a Verified badge until
they add it. The catalog reads `tested.json` from `main` every night; adding it later needs no new release.

## 11. Dry-run the catalog on the published release
```
rm -rf ../reg ../catout && mkdir -p ../reg && cp packaging/catalog/sfizz.json ../reg/
GITHUB_TOKEN=$(gh auth token) python3 ../kit/tools/catalog_build.py --registry ../reg --out ../catout \
  --cache ../catcache --yanked ../kit/catalog/yanked.json
python3 - <<'EOF'
import json
c = json.load(open("../catout/catalog.json")); p = [x for x in c["plugins"] if x["id"] == "sfizz"][0]
v = [x for x in p["versions"] if x["version"] == "1.0.4"]
print("latest:", p.get("latest"), "| assets:", sorted(v[0].get("assets", {})) if v else None, "| gen2:", v[0].get("gen2") if v else None)
print("problems:", json.load(open("../catout/problems.json")))
EOF
```
Check: `latest: 1.0.4 | assets: ['aarch64', 'armv7'] | gen2: True` and `problems: []`. If the aarch64 asset is missing
or problems are listed, stop and report them. (It may also print a "quality bar" line naming what the listing could
still improve, such as a CPU bench: that is advice, not a failure.)

## 12. Open the catalog PR (first listing only)
```
cd ..   # beside mpc-vst-sfizz/
gh repo fork sd88me/mpc-vst-plugins --clone --remote && cd mpc-vst-plugins
git checkout -b add-sfizz
cp ../mpc-vst-sfizz/packaging/catalog/sfizz.json catalog/plugins/sfizz.json
python3 tools/catalog_build.py --check-registry                                          # check: "0 problems"
curl -sI https://raw.githubusercontent.com/gmorb/mpc-vst-sfizz/v1.0.4/docs/screen.png | head -1   # check: 200
git add catalog/plugins/sfizz.json && git commit -m "Add sfizz (SFZ sampler, Gen1 + Gen2)"
git push -u origin add-sfizz
gh pr create --repo sd88me/mpc-vst-plugins --head gmorb:add-sfizz --title "Add sfizz" --body-file - <<'EOF'
Adds **sfizz**: the sfizz 1.2.3 SFZ sampler for MPC OS (gmorb/mpc-vst-sfizz, BSD-2-Clause).

- Release v1.0.4 carries both builds: `sfizz-1.0.4-mpc-armv7.zip` (Gen1) and `sfizz-1.0.4-mpc-aarch64.zip` (Gen2).
- Both pass `tools/catalog_check.py --catalog` (OK, 0 warnings); a local `catalog_build.py` run lists 1.0.4 with both assets and no problems.
- One registry entry; the Gen2 zip is found by the armv7 → aarch64 swap.
- Release: RELEASE_URL
EOF
gh pr view --repo sd88me/mpc-vst-plugins --json number,url,state                       # check
```
Before running `gh pr create`, replace `RELEASE_URL` with the real URL from step 9's `gh release view` output.
After it is opened, watch the PR's checks: `gh pr checks <number> --repo sd88me/mpc-vst-plugins --watch`. Fix only what
the checks name, in this one-file entry, and report anything you can't fix. Later releases need no PR: the catalog
picks up new releases from the repo by itself.

## 13. Final report
For each step 1–12: what you ran, the key lines of output, and done / not done / failed (with why). End with the real
URLs: repo, release, PR. List what is left for the owner: the social preview image, a Gen2 device test if none was
recorded, and merging of the PR by the catalog maintainer.
