# GASNet Repository Migration

## The GASNet project has MOVED to GitHub

The GASNet project is now hosted at GitHub, within the Berkeley Lab organization:

### [GASNet Home Page](https://gasnet.lbl.gov) <https://gasnet.lbl.gov>

### [GASNet Git Repository](https://github.com/berkeleylab/gasnet) <https://github.com/berkeleylab/gasnet>

### [GASNet Issue Tracker](https://gasnet-bugs.lbl.gov) <https://gasnet-bugs.lbl.gov>

## Master branch renamed

As a part of the migration, the `master` branch has also been renamed to `main`,
to improve alignment with updated industry norms.

## How do I update my existing git working directory?

If you have a local git checkout of the GASNet repository,
you can update the `origin` remote with a command like the following:

```bash
git remote set-url origin https://github.com/BerkeleyLab/gasnet.git
```

Then, assuming you want to use the `main` branch, you can checkout and then
reset a local `main` branch to match the updated upstream remote with commands
like the following:

```bash
git stash                         ;: Saves any uncommited changes
git fetch origin                  ;: Fetches from the updated remote
git checkout main                 ;: Ensure we are on the correct local branch
git tag old-repo-state            ;: Tags the current commit
git reset --hard origin/main      ;: Resets branch state to match remote
```

The `stash` and `tag` operations preserve any local work you might have.

## Why did we make this change?

For years, the GASNet development team has observed that Atlassian has
repeatedly and consistently prioritized corporate profit over providing a
stable and productive Git hosting platform, to the increasing detriment of free
and open-source software hosted on Bitbucket Cloud.

The services provided by Bitbucket Cloud have now degraded to the point where
we no longer consider it viable for open-source development.

