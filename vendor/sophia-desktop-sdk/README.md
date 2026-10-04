# Pinned C desktop SDK

`source/` is an immutable archive of the signed SDK revision in `manifest.json`.
It builds offline and contains its own contract and import provenance. The
manifest covers every source file; `python3 scripts/check-sophia-vendor.py`
rejects modified, missing, extra, or symlinked files. Sophia's integration gate
separately compares the SDK contracts and vectors with its authoritative copies.
It hashes Git blobs/trees offline (including executable modes), then checks that
the raw `upstream.commit` has both the declared revision and that source tree.
Signature authorization remains a release/review step; hashing is not signature
verification. No object database or network fetch is needed for this check.

Change code in sophia-org/sophia-desktop-sdk-c, test it, and sign the commit.
Then replace `source/` from `git archive <exact revision>`, save
`git cat-file commit <exact revision>` as `upstream.commit`, regenerate the sorted
SHA-256 file manifest with that revision, and run the snapshot check and C gates.
Do not edit the snapshot or use a moving branch as its identity.

This pin is the signed C SDK v0.8.0 release on the SDK's public master branch.
Its lock provider role is experimental and unused by Bemenu.
Sophia's production export tests and Bemenu's isolated strict gate cover this
revision. Those fixtures do not establish live desktop acceptance. This
snapshot does not require network access.
