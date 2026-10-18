# Release sfizz for MPC OS 1.1.0 (SoundFont 2, Mono / Legato with Glide): instructions for an agent

1.0.4 is already published on GitHub as `gmorb/mpc-vst-sfizz` and listed for the MPC OS plugin catalog (PR
`sd88me/mpc-vst-plugins#476`). These instructions publish **1.1.0** on top of it and update the catalog listing's
text. Follow them in order. Each step has a **check**: run it and show its real output before you call the step done.

## Rules
- **Never report a GitHub state you didn't just see in command output.** "Pushed", "tagged", "uploaded",
  "published", "PR updated" each need the command that did it and the command that checked it. Every URL you report
  comes from command output (`--json url`). If a step fails or you can't run it, report "not done" and why.
- **Upload the delivered files exactly as they are.** Don't rebuild, re-zip or rename them: the checksums below prove
  the files the owner tests are the files people download.
- Never force-push, delete or move a tag, rewrite history, or create a second release for a tag. The new commits must
  apply as a **fast-forward** on top of GitHub's `main`; if they don't, **stop and ask**.
- Use `gh` for everything it can do.
- Two hard stops: step 7 (the owner tests the draft) and step 9 (the owner gives the device facts). Don't continue
  past a stop without the owner's go.

## Fixed facts
| | |
|---|---|
| GitHub repo | `gmorb/mpc-vst-sfizz`, default branch `main`; GitHub's `main` must currently be `493365d` ("tested.json: 1.0.4 device tests") |
| New tag / release title | `v1.1.0` / `sfizz for MPC OS 1.1.0` |
| Gen1 asset | `sfizz-1.1.0-mpc-armv7.zip`, sha256 `d8f7ca0ffac31aced2e06c2845b5796cdbb58add8a0c6247b60e94f08293df0e`, 1501639 bytes |
| Gen2 asset | `sfizz-1.1.0-mpc-aarch64.zip`, sha256 `64d7b78a1d5b036ccd2d2776f226d76dc96dd01a46fa6cc0edd9d23b53443a67`, 1582218 bytes |
| Standalone asset | `sfizz-for-MPC-OS-1.1.0.zip`, sha256 `6e1a0531226436cc0ad4b17f3a2e20e2e7c369bf828a2a6294ea193bab11f2ea`, 1510331 bytes |
| Other assets | `sfizz-for-MPC-OS-1.1.0-manual.pdf` (802510 bytes), `SHA256SUMS` (379 bytes) |
| Release notes | `packaging/release-notes-v1.1.0.md` (in the repo after step 3) |
| Catalog entry | `packaging/catalog/sfizz.json` (new summary and tags: SoundFont support) → `catalog/plugins/sfizz.json` |
| Kit commit for checks | `e18a0a1da84515096d354f399dd4cd7f9c079fb5` (sd88me/mpc-vst-plugins) |

## 0. Inputs
The owner gives you these files. Put them in one empty working folder:
- `mpc-vst-sfizz-1.1.0.bundle`: the new commits, as a git bundle on top of `493365d`
- `sfizz-1.1.0-mpc-armv7.zip`, `sfizz-1.1.0-mpc-aarch64.zip`, `sfizz-for-MPC-OS-1.1.0.zip`, `SHA256SUMS`
- `sfizz-for-MPC-OS-1.1.0-manual.pdf`
```
mkdir -p rel && mv sfizz-1.1.0-mpc-*.zip sfizz-for-MPC-OS-1.1.0.zip sfizz-for-MPC-OS-1.1.0-manual.pdf SHA256SUMS rel/
(cd rel && sha256sum -c SHA256SUMS)                       # check: all four files "OK"
git bundle verify mpc-vst-sfizz-1.1.0.bundle              # check: "is okay", requires 493365d
```
If a checksum or the bundle check fails, stop and ask.

## 1. Preflight
```
gh auth status                                            # check: logged in as gmorb, scopes include repo
gh api user --jq .login                                   # check: gmorb
```

## 2. State
```
R=gmorb/mpc-vst-sfizz
git ls-remote https://github.com/$R main                  # check: 493365d...
git ls-remote --tags https://github.com/$R                # check: v1.0.4 only, no v1.1.0
gh release list --repo $R
gh pr view 476 --repo sd88me/mpc-vst-plugins --json state,mergedAt,headRefName,url
```
If `main` is not `493365d`, or `v1.1.0` already exists, stop and ask. Note whether PR #476 is OPEN or MERGED (step 10).

## 3. Add the new commits and push `main`
```
git clone https://github.com/gmorb/mpc-vst-sfizz && cd mpc-vst-sfizz
git pull --ff-only ../mpc-vst-sfizz-1.1.0.bundle main     # must say "Fast-forward"
git log --oneline -6                                      # the new commits on top of 493365d
grep -n '"version"' vst.json                              # check: 1100
git push origin main
git ls-remote https://github.com/gmorb/mpc-vst-sfizz main ; git rev-parse HEAD     # check: same commit
```

## 4. Tag
```
git tag -a v1.1.0 -m "sfizz for MPC OS 1.1.0" && git push origin v1.1.0
git ls-remote --tags https://github.com/gmorb/mpc-vst-sfizz v1.1.0                 # check: the tag exists
```

## 5. Draft release with the five assets
```
gh release create v1.1.0 --repo gmorb/mpc-vst-sfizz --draft --verify-tag \
  --title "sfizz for MPC OS 1.1.0" --notes-file packaging/release-notes-v1.1.0.md \
  ../rel/sfizz-1.1.0-mpc-armv7.zip ../rel/sfizz-1.1.0-mpc-aarch64.zip ../rel/sfizz-for-MPC-OS-1.1.0.zip \
  ../rel/sfizz-for-MPC-OS-1.1.0-manual.pdf ../rel/SHA256SUMS
gh release view v1.1.0 --repo gmorb/mpc-vst-sfizz --json tagName,isDraft,isPrerelease,url,assets \
  --jq '{tag:.tagName,draft:.isDraft,pre:.isPrerelease,url:.url,assets:[.assets[]|{name,size}]}'
```
Check: `draft: true`, `pre: false`, five assets with the sizes in Fixed facts.

## 6. Check the uploaded files the way the catalog will
Scratch folders go beside the repo, never inside it.
```
rm -rf ../dl && gh release download v1.1.0 --repo gmorb/mpc-vst-sfizz -D ../dl
(cd ../dl && sha256sum -c SHA256SUMS)                                               # check: all four OK
git clone -q https://github.com/sd88me/mpc-vst-plugins ../kit && git -C ../kit checkout -q e18a0a1da84515096d354f399dd4cd7f9c079fb5
for z in ../dl/sfizz-1.1.0-mpc-armv7.zip ../dl/sfizz-1.1.0-mpc-aarch64.zip; do
  python3 ../kit/tools/catalog_check.py $z --catalog --expect-id sfizz --expect-repo gmorb/mpc-vst-sfizz; done
```
Check: both end `OK: sfizz 1.1.0 (0 warnings)`.

## 7. STOP: report and wait
Report the state (step 2), the pushed commit, the draft release URL, the assets with sizes and the check output. Ask
the owner to test the draft's zips on the device, especially SoundFonts (TESTING.md, "On the device"):
1. copy an `.sf2` to a card; ⌄ → the card → the SoundFont opens with its presets;
2. tap a preset: "Converting…", then it plays; tap it again later: it loads at once;
3. ‹ › step through the presets; 4. a program change switches presets (if MPC sends them to plugins);
5. save and reload a project with a SoundFont preset;
6. Settings › Playing: Mode Mono, then Legato, with Glide Off and about 200 ms (one note at a time; legato without a new
   attack; the pitch slide); Poly plays as before;
and check that SFZ instruments still load and play as before.
Wait for **go**. If the owner reports a problem, stop: don't publish.

## 8. Publish (only after go)
```
gh release edit v1.1.0 --repo gmorb/mpc-vst-sfizz --draft=false --latest
gh release view v1.1.0 --repo gmorb/mpc-vst-sfizz --json isDraft,url                # check: isDraft false
curl -sI https://github.com/gmorb/mpc-vst-sfizz/releases/download/v1.1.0/sfizz-1.1.0-mpc-armv7.zip | head -1     # HTTP/2 302
curl -sI https://github.com/gmorb/mpc-vst-sfizz/releases/download/v1.1.0/sfizz-1.1.0-mpc-aarch64.zip | head -1   # HTTP/2 302
```

## 9. Record the device tests (`tested.json`)
Ask the owner which devices they tested **1.1.0** on (model, MPC OS version, date). **Never guess.** A Gen2 test needs
"Gen2" in `device`. Add one line per device to the existing array in `tested.json` (keep the 1.0.4 lines), e.g.
`{ "version": "1.1.0", "device": "MPC Live III (Gen2)", "firmware": "3.9.1", "date": "YYYY-MM-DD" }`, then:
```
python3 -m json.tool tested.json >/dev/null && git add tested.json && git commit -m "tested.json: 1.1.0 device tests" && git push origin main
gh api repos/gmorb/mpc-vst-sfizz/contents/tested.json --jq .content | base64 -d     # check: the new lines
```
Without these lines the catalog shows 1.1.0 without the Verified badges until they're added. If the owner tested
nothing, skip this step and say so.

## 10. Update the catalog listing's text
The catalog picks up v1.1.0 by itself (no PR needed for a new version). Only the listing's summary and tags changed
(they now mention SoundFonts and mono/legato with glide), in `packaging/catalog/sfizz.json`.
- **PR #476 still OPEN**: update its branch.
  ```
  cd .. && gh repo clone gmorb/mpc-vst-plugins fork && cd fork && git checkout add-sfizz
  cp ../mpc-vst-sfizz/packaging/catalog/sfizz.json catalog/plugins/sfizz.json
  python3 tools/catalog_build.py --check-registry                                  # check: 0 problems
  git commit -am "sfizz: mention SoundFont 2 and mono/legato support" && git push origin add-sfizz
  gh pr view 476 --repo sd88me/mpc-vst-plugins --json headRefOid,url               # check: the new commit
  ```
- **PR #476 MERGED**: open a small follow-up PR from a new branch on the fork, synced with upstream first.
  ```
  cd .. && gh repo clone gmorb/mpc-vst-plugins fork && cd fork
  gh repo sync gmorb/mpc-vst-plugins --source sd88me/mpc-vst-plugins && git pull origin main
  git checkout -b sfizz-sf2
  cp ../mpc-vst-sfizz/packaging/catalog/sfizz.json catalog/plugins/sfizz.json
  python3 tools/catalog_build.py --check-registry                                  # check: 0 problems
  git commit -am "sfizz: mention SoundFont 2 and mono/legato support" && git push -u origin sfizz-sf2
  gh pr create --repo sd88me/mpc-vst-plugins --head gmorb:sfizz-sf2 --title "sfizz: mention SoundFont 2 and mono/legato support" \
    --body "sfizz 1.1.0 plays SoundFont 2 (.sf2) files and adds mono/legato with glide: summary and tags updated. Release: <the real v1.1.0 URL from step 8>"
  gh pr view --repo sd88me/mpc-vst-plugins --json number,url,state                 # check
  ```
  Replace the placeholder with the real URL before running `gh pr create`.

## 11. Final report
For each step: what you ran, the key output, and done / not done / failed (with why). End with the real URLs:
release, catalog PR. Remind the owner that the social preview image can be replaced with the new
`docs/images/banner.png` (Settings › General › Social preview), which now mentions SoundFont 2.
