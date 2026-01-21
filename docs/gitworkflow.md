# FarmPulse – Git Workflow (Industry Standard)

This repository follows a **simple, disciplined Git workflow**.
The goal is clarity, parallel development, and zero chaos.

There are **three branch types only**:
- `main` → production / release-ready code
- `develop` → integration branch
- `driver/*` or `feature/*` → task-specific work

No direct commits to `main` or `develop`.

- `main` is protected
- `develop` is the default integration branch

---

## Absolute Rules (Do Not Break These)

- ❌ No commits directly to `main`
- ❌ No force-push to shared branches
- ❌ No mixing multiple features in one branch
- ✅ Always build before pushing

Git is the system of record.
Respect it.

---

## Mental Model

> `main` = what runs in the field  
> `develop` = what we trust next  
> `your branch` = where mistakes are allowed

Simple rules. Strong teams.

---
---

## 1. Branching Strategy (How Work Is Organized)

### Naming Convention (Mandatory)

```text
driver/lora
feature/oled-ui
feature/gateway-comm
bugfix/spi-timeout
```

**Rules**
- One task → one branch
- Short, descriptive, lowercase
- No personal names in branch names

---

## 2. Creating a New Working Branch

Every task starts from `develop`.

```bash
git checkout develop
git pull origin develop
git checkout -b driver/lora
```

You are now isolated.
Nothing you break affects the team.

---

## 3. Making Changes (Local Work)

Edit code as required.
Keep changes focused.

Check what you changed:

```bash
git status
```

---

## 4. Staging Changes

Stage **only relevant files**.

```bash
git add path/to/file.c
```

Avoid:
```bash
git add .
```

Precision matters.

---

## 5. Committing Changes

Each commit must represent **one logical change**.

### Commit Message Format

```text
type: short, clear description
```

Example:

```bash
git commit -m "driver: improve lora tx non-blocking flow"
```

Common types:
- `driver:`
- `feature:`
- `fix:`
- `chore:`
- `docs:`

---

## 6. Pushing Your Branch

Push your branch to the remote repository:

```bash
git push -u origin driver/lora
```

This publishes your work without merging it.

---

## 7. Keeping Your Branch Updated

Before merging, always sync with `develop`:

```bash
git checkout develop
git pull origin develop
git checkout driver/lora
git merge develop
```

Fix conflicts locally.
Do not push broken builds.

---

## 8. Merging Into `develop`

### Preferred Method: Pull Request (Recommended)

1. Push your branch
2. Open a Pull Request → `driver/lora` → `develop`
3. Request review
4. Ensure build passes
5. Merge after approval

### Direct Merge (Small Teams Only)

```bash
git checkout develop
git merge driver/lora
git push origin develop
```

Delete branch after merge:

```bash
git branch -d driver/lora
git push origin --delete driver/lora
```

---