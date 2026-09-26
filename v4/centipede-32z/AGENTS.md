# Workspace Rules for centipede-32z

## FUNDAMENTAL RULE NUMBER ONE: Only Change Files Within This centipede-32z Directory Tree

- **Only change files within this `centipede-32z` directory tree (`/home/strick/modoc/coco-shelf/tfr9/v4/centipede-32z/`).**
- If you find a bug or want to change a file outside this directory tree:
  1. **Stop everything immediately.**
  2. Let the user know exactly what is needed, which external files/projects are affected, and why.
  3. Wait for the user to coordinate the change with other projects and teams.

See the skill at [`.agents/skills/fundamental/SKILL.md`](.agents/skills/fundamental/SKILL.md) for details.

## RULE NUMBER TWO: Git Commits Only on "work*" Branches

- **Only commit changes to a git branch starting with the prefix `work`** (e.g., `work`, `work-sept25-try-coco3`).
- **Committing to `main` or other branches is for the user to do.**
- You may inspect and examine other branches, but **never change them**.

See the skill at [`.agents/skills/git/SKILL.md`](.agents/skills/git/SKILL.md) for details.
