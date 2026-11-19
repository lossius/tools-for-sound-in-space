# Releasing

A release is: the four plugins (TriPan, DistPan, VecPan, ViMiC) built for macOS (signed and notarized, so that macOS does not block them) and Windows,
published as downloads on GitHub. Almost everything is one command: `scripts/release.sh`.

## Where you are

Run this at any time; it checks every prerequisite and says what is missing:

```
scripts/release.sh check
```

## One-time setup

1. **Signing certificate** (done on this Mac): a "Developer ID Application" certificate in the keychain. The script finds it by itself.
2. **Notarization login** (done on this Mac): Apple scans the plugins and approves them. The script uses a login stored in the keychain under the name
   `tripan-notary`. On another Mac, make it once with
   `xcrun notarytool store-credentials tripan-notary --apple-id <your Apple ID> --team-id <TEAM ID>` (it asks for an *app-specific password*, made at
   appleid.apple.com under Sign-In and Security).
3. **GitHub repository** (not done yet):
   - Make an account at github.com if you have none, then create a new empty repository (no README, no licence; the code has both). Public, since the licence is AGPL.
   - Connect this folder to it and push (replace the address with the one GitHub shows you):
     ```
     git remote add origin https://github.com/<you>/<repository>.git
     git push -u origin main
     ```
   - Open the **Actions** tab of the repository once: the first run builds and tests everything on macOS and Windows. It must be green before the first release;
     if it is red, send me the log.
4. **GitHub command line tool** (lets the script create the release and upload the macOS files):
   ```
   brew install gh
   gh auth login
   ```
   (choose GitHub.com, HTTPS, and log in through the browser). Without it the script still works and tells you to attach the files by hand.

## Making a release

1. Commit everything (`git status` shows nothing).
2. `scripts/release.sh publish 0.1.0` (use the version number you want). It
   - runs the checks,
   - builds all four plugins as universal macOS binaries (Apple silicon and Intel) and runs all tests,
   - signs them, sends them to Apple for notarization (about 12 submissions, a few minutes each; the script waits), staples the approval and zips them into `dist/`,
   - sets the version, commits it, tags `v0.1.0` and pushes,
   - creates the GitHub release with the macOS zips and a checksum file.
3. The push starts the build of the Windows files on GitHub (Actions tab, about 15 to 30 minutes); they are added to the same release when it is done.
4. Open the release page, read the generated notes, and edit them if you want to (what is new, install instructions).

To try everything except publishing: `scripts/release.sh build`.

## What users do

- **macOS**: unzip and copy the plugin into `~/Library/Audio/Plug-Ins/VST3` or `.../Components` (AU); the standalone app goes in Applications.
- **Windows**: copy the `.vst3` folder into `C:\Program Files\Common Files\VST3`. The Windows files are not signed, so Windows SmartScreen may warn on first use
  ("More info", "Run anyway").
- AAX (Pro Tools) is not part of this: it needs Avid's signing (see README).

## If something goes wrong

- *Notarization is rejected*: the script prints Apple's reason; `xcrun notarytool log <submission id> --keychain-profile tripan-notary` shows the details.
- *The tag is wrong*: before anyone has downloaded it, delete the release on GitHub and run `git tag -d v0.1.0; git push origin :refs/tags/v0.1.0`, fix, and publish again.
- *Signing in GitHub's own build* (optional, not needed with this setup): the workflow can also sign and notarize on GitHub if the secrets listed in README.md are set.
