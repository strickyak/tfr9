---
name: git-skill
description: >-
  Git workflow and branch modification policy. Use whenever staging, committing,
  or manipulating git branches. Enforces that commits may only be made to branches
  starting with "work", while other branches are strictly read-only.
---

# Git Skill: Branch Modification Policy

## Core Rule: Only Commit to "work*" Branches

- **You may ONLY commit changes to a git branch starting with the letters `work`** (e.g., `work`, `work-tfr-sept-21`, `work-feature-x`).
- **You may look at other branches, but NOT change them.** All non-`work*` branches (such as `main`, `saturn`, `stage1`, etc.) are strictly read-only.

---

## Pre-Commit Verification Procedure

Before running `git commit`, always verify the current branch:

```bash
git branch --show-current
```

1. **If the branch name starts with `work`** (e.g., `work-tfr-sept-21`):
   - You may proceed with staging and committing files that belong to this workspace folder.

2. **If the branch name does NOT start with `work`** (e.g., `main`, `chelsea`):
   - **DO NOT COMMIT.**
   - Either switch to an existing `work*` branch or create a new branch prefixed with `work`:
     ```bash
     git checkout -b work-<descriptive-name>
     ```

---

## Read-Only Branch Rules

- You may freely inspect, view, diff, log, or checkout other branches for reference, research, and study.
- You must **NEVER** make commits on, merge into, cherry-pick into, reset, rebase, or push to non-`work*` branches.
