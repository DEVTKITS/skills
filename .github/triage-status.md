# Issue Triage & Health Dashboard

**Last Updated**: 2026-07-24 | **Total Open Issues**: 100 | **Health**: ⚠️ Needs Triaging

---

## Executive Summary

The skills repository currently has **100 open issues** spanning bugs, feature requests, proposals, and discussions. This document establishes a labeling and triage strategy to improve issue health, contributor onboarding, and roadmap clarity.

### Key Findings
- **5 Critical Bugs**: Blocking users (Codex issues, setup template, GitLab commands, CDN/SRI hooks, Grill skill)
- **18 High-Priority Items**: User-validated features and impactful bugs
- **45 Medium-Priority Items**: Nice-to-have improvements and exploratory ideas
- **32 Low-Priority Items**: Discussions, long-term proposals, feedback

**Top Issue Contributors**: wayfinder (12), grilling (9), setup (8), implement (7), code-review (6)

---

## Issue Categorization

### 🐛 BUGS (23 issues)

Issues where functionality is broken or behaves incorrectly.

#### Critical (5)
- **#651**: CDN-loaded Tailwind/Mermaid silently breaks HTML report — SRI hooks not initialized for offline environments
- **#645**: GitHub/GitLab setup templates still reference pre-v1.1 "PRD" terminology → should be "Spec"
- **#635**: GitLab issue-tracker commands broken in `/setup-matt-pocock-skills`
- **#622**: Handoff skill doesn't work in Codex environment
- **#644**: Grill skill has unspecified problem (needs clarification from user #644)

#### High (8)
- **#625**: wayfinder: Codex ignores `wayfinder:grilling` metadata tag
- **#620**: wayfinder: small-effort escape hatch stops instead of degrading into full grilling
- **#616**: Triage labels missing or incomplete
- **#607**: TDD skill: using when fixing issues causes agent to prompt for test seam
- **#606**: Parent spec/PRD issue incorrectly labeled "ready-for-agent"
- **#599**: `/wayfinder` should not do remote changes by default
- **#573**: code-review sub-agents recursively invoke `/code-review` (fan-out issue)
- **#570**: grilling session switches immediately to implementation without break

#### Medium (10)
- **#523**: `install` CLI: SyntaxError — missing `styleText` export from `node:util`
- **#512**: `install` UI broken on fish shell
- **#511**: `/implement` and `/code-review` disagree on what counts as uncommitted changes
- **#508**: `/implement` has no completion step — tickets never marked done
- **#507**: Wayfinder sub-issue numbers incorrectly included in text as GitHub issue numbers
- **#498**: `/implement` can enter infinite `/code-review`→fix cycles without stopping condition
- **#576**: `wayfinder:research` creates draft PRs as unwanted side effect
- **#569**: `/wayfinder` map creates tickets marked "blocked by" instead of SubIssues
- **#568**: `/implement` doesn't mark local markdown issues as resolved before commit
- **#515**: Wayfinder incorrectly labels decision tickets as "ready-for-agent"

---

### ✨ FEATURE REQUESTS (20 issues)

New capabilities, enhancements, or improvements requested by users.

#### High-Impact (6) — User-Validated
- **#652**: Add async issue mode for grilling skill — batch findings instead of one-by-one questions for GitHub issue workflows
- **#649**: Add pacing guardrails & hybrid syntax highlighting to teach skill
- **#633**: Add optional `/review-spec` skill for independent pre-implementation review
- **#618**: `/implement` support for auto-sequencing multiple tickets via subagents
- **#594**: Add `explain-diff` skill to understand agent-generated code changes
- **#609**: New skill to complete Sandcastle workflow

#### Medium (10)
- **#610**: Allow install individual skills via Claude Code marketplace (don't require full bundle)
- **#605**: Create sandcastle setup skill
- **#604**: Consider automated security reviews as release gate
- **#589**: Update "tdd" skill description to match documented workflow
- **#588**: Suggestion: First-Principles Engineering Skill (`/first-principles`)
- **#521**: Proposal: `/to-commit` and `/to-pr` skills
- **#520**: Ability to register other skills to run when also running review skill
- **#509**: Let `/implement` open a pull request instead of committing
- **#493**: Skill idea: `implement-with-worktree` (parallel sessions without conflicts)
- **#489**: Skill idea: implementation walkthrough / guided QA

#### Lower (4)
- **#611**: Teach skill "Check yourself"
- **#642**: `/teach` me dark mode (feature request for teach skill)
- **#567**: Obsidian kanban board support for issue tracker
- **#510**: Support for plugins (ChatGPT)

---

### 💡 PROPOSALS (25 issues)

Design ideas, architectural improvements, or workflow changes needing discussion before implementation.

#### Grilling/Workflow Design (10)
- **#652** (also feature): Async grilling mode (already counted above)
- **#643**: Use AskQuestion for structured input during grilling sessions
- **#632**: Add optional roadmap label lifecycle to setup
- **#628**: wayfinder: explicit "one grill per fresh session" when ideas surface mid-map
- **#631**: Could `--fork-session` simplify handoff + prototype flow?
- **#648**: Matt Pocock Skills workflow improvement suggestion
- **#619**: Document one-ticket-per-context prompt as lightweight Ralph loop alternative
- **#617**: handoff: end the doc with a ready-to-run launcher
- **#597**: `/grilling`: Add progress header "Question N of ~M"
- **#590**: Enhance TDD skill with two-phase workflow to reduce attention dilution

#### Architecture/Infrastructure (15)
- **#613**: Should Specs reference governing ADRs? Should Tickets reference parent Specs?
- **#557**: Decouple ADRs from domain-modelling
- **#531**: Introduce Sub-Agent framework to encapsulate complex skills (TDD, Code Review)
- **#564**: Make architecture subagent workflows harness-neutral
- **#561**: Make code-review subagent orchestration harness-neutral
- **#558**: Make `setup-matt-pocock-skills` harness-aware (choose AGENTS.md or CLAUDE.md)
- **#560**: Add documentation impact gate before implementation closeout
- **#526**: `to-spec`: search issue tracker for related work before drafting
- **#525**: Use heading-based template for local markdown issues
- **#524**: Register other skills to run with review skill
- **#574**: Idea: "cold read" step between `/to-tickets` and `/implement`
- **#566**: Idea: "skeptic" skill that refuses to trust code-review
- **#581**: Suggestion: Separate `/code-review` from `/implement`
- **#614**: `ask-matt` should verify SKILL.md before asserting or skipping workflow steps
- **#491**: Output next steps after each `/wayfinder` iteration

---

### 💬 DISCUSSIONS (15 issues)

Questions, field reports, user feedback, and exploratory ideas.

#### Field Reports (8)
- **#630**: Field report: running these skills in a two-person, two-agent team
- **#629**: Field data: grill-with-docs-style deliberate state under two writers
- **#562**: Feedback from using claude-handoff
- **#578**: Does anyone use this with gpt-5.6-sol?
- **#644**: "I have noticed a problem with Grill" (vague; needs clarification)
- **#637**: "why removed caveman" (question about skill removal)
- **#646**: "When is version 1.2 coming out" (release question)
- **#621**: Request to add opencode support

#### Design Questions (7)
- **#500**: Should `/wayfinder` replace or complement `/grill-with-docs`?
- **#528**: Wayfinder response defaults to prose (should offer structured alternatives)
- **#527**: grill-me/grilling scoped to plans/designs — should also support opinions/claims
- **#486**: V1.1 outdated local issue tracker terminology (PRD → Spec)
- **#483**: `/code-review` name clash with Claude Code built-in

---

### 🛠 MAINTENANCE (15 issues)

Documentation, tooling, repository health, metadata, and infrastructure.

- **#615**: Use of `skills.sh.json` on repository (unclear purpose)
- **#559**: `skills/productivity/teach/GLOSSARY-FORMAT.md` is an orphan (unused)
- **#639**: Some skills flagged by Nvidia's Skillspector as "DO NOT USE"
- **#624**: Advertise repo as Pi package to ship Pi version of Claude plugin
- **#517**: Vercel Skills installs from main, doesn't respect release tags
- **#592**: Add ticket ID for file-based markdown issue systems
- **#555**: Tickets not created with GitHub REST API dependencies available
- **#554**: Tickets not created as sub-issues when using `/to-tickets`
- **#553**: `to-tickets` didn't create blocking relationships
- **#514**: TDD: green-bar patterns, GOOS mocking guidance (branch ready, validated)
- **#520**: Ability to register other skills when running review skill
- **#601**: Prototype complete user journey — wayfinder milestone
- **#563**: 希望增加会议录音转写 + 会议记录 (Chinese: request for meeting transcription features)
- **#579**: wayfinder: reconcile durable context documents across branches/worktrees
- **#492**: `/wayfinder` cannot invoke `/to-spec`

---

## Statistics

### By Type
| Type | Count | % |
|------|-------|---|
| Bugs | 23 | 23% |
| Feature Requests | 20 | 20% |
| Proposals | 25 | 25% |
| Discussions | 15 | 15% |
| Maintenance | 15 | 15% |
| **TOTAL** | **100** | **100%** |

### By Priority (Recommended)
| Priority | Count | Next Action |
|----------|-------|-------------|
| **Critical** | 5 | Assign to sprint, unblock immediately |
| **High** | 18 | Ready for implementation, validate scope |
| **Medium** | 45 | Groom into future releases |
| **Low** | 32 | Backlog, revisit quarterly |

### By Skill Area (Top 5)
| Skill | Count | Status |
|-------|-------|--------|
| wayfinder | 12 | Needs attention (map, research, charting) |
| grilling | 9 | Async mode + UX improvements needed |
| setup | 8 | Terminology updates, GitLab fixes |
| implement | 7 | Completion tracking, decision points |
| code-review | 6 | Recursion control, orchestration |

---

## Labeling Strategy

### Proposed Labels

When permissions are available, apply labels to issues using this schema:

#### Type Labels
- `bug` — broken functionality, errors, incorrect behavior
- `feature-request` — new capability, enhancement
- `proposal` — architectural/design idea, needs discussion
- `discussion` — question, field report, feedback
- `maintenance` — docs, tooling, health, metadata

#### Skill Labels
- `skill/grilling`
- `skill/wayfinder`
- `skill/to-tickets` / `skill/to-spec`
- `skill/tdd`
- `skill/code-review` / `skill/implement`
- `skill/teach`
- `skill/handoff`
- `skill/setup`
- `skill/architecture`
- `skill/domain-modeling`

#### Priority Labels (when added to repo)
- `priority/critical` — blocking users, high-impact bugs
- `priority/high` — user-validated, high-impact features
- `priority/medium` — good-to-have, nice improvements
- `priority/low` — exploratory, long-term

#### Status Labels
- `ready-for-agent` — clear spec, ready for AFK implementation
- `needs-triage` — unclear, needs categorization
- `blocked` — depends on other work
- `ready-for-human` — needs human decision before proceeding

---

## Quick-Start Actions for Contributors

### 1. Pick a Label Set
Choose one category to start:
- **Help Wanted**: Critical bugs (unblock users)
- **Enhancement**: Feature requests (user-validated features)
- **Proposal**: Design ideas (needs discussion)
- **Question**: Field data (gathers intelligence)

### 2. How Issues Move Through States

```
[Needs Triage] → [Ready for Agent] → [In Progress] → [Done]
```

- **Needs Triage**: Unclear requirements, needs discussion
- **Ready for Agent**: Spec is clear, ready for automated implementation
- **In Progress**: Being worked on
- **Done**: Merged/resolved

### 3. Contributing Guidelines

When picking an issue to work on:

1. **Read the issue fully** — understand context and any linked docs
2. **Check dependencies** — make sure related issues are resolved first (look for "blocked by")
3. **Ask for clarification** — if spec is unclear, comment to ask questions (don't assume)
4. **Link work** — if your work resolves this issue, link it in your PR
5. **Reference the issue** in your commit message: `Closes #XXXX` or `Fixes #XXXX`

---

## Roadmap Priority

### 🚨 Immediate (This Sprint)
Focus on 5 critical bugs:
1. **#622**: Handoff in Codex (impacts users on Claude Code)
2. **#651**: CDN/SRI hooks (affects offline + security)
3. **#645**: Setup terminology (confuses new users)
4. **#635**: GitLab setup broken (hard blocker for GitLab users)
5. **#644**: Grill skill problem (needs reproduction/clarification first)

### 📊 Next (Weeks 2-4)
**High-priority features + bugs**:
- **#652**: Async grilling mode (field-validated, 6 months of field data)
- **#625**: wayfinder metadata in Codex (breaks workflow)
- **#620**: wayfinder escape hatch (affects user flow)
- **#616**: Triage labels missing (infrastructure)
- **#649**: Teach skill pacing guardrails (UX)

### 🔄 Later (Next Quarter)
**Medium + Low priority**:
- Proposals (#531, #557, #560, #614 — architecture improvements)
- Nice-to-have features (#610, #605, #588, #521)
- Long-term ideas (#567, #510, #563 — integrations)

---

## How to Update This Document

When triaging new issues:

1. Note the issue number, title, and description
2. Categorize by type (bug/feature/proposal/discussion/maintenance)
3. Estimate priority (critical/high/medium/low)
4. Identify related skill(s)
5. Add to the relevant section above
6. Update totals and skill counts
7. Commit with message: `docs: update triage status (#XXXX adds new issue)`

---

## References

- **CLAUDE.md** — domain language and terminology
- **CONTRIBUTING.md** — contributor guidelines (link when created)
- **.github/ISSUE_TEMPLATE/** — issue templates (check for updates)
- **ADRs** in `.agents/adr/` — architecture decisions
