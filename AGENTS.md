# AGENTS.md

## Project Context
- This project is a C++ OpenGL cloth simulation portfolio project.
- Prioritize readable structure, maintainability, and stable execution.
- Consider memory usage and runtime performance when designing or modifying code.
- Project world coordinates use Y-up.

## Communication
- Answer in Korean.
- Keep explanations concise.
- Default to short answers: 3-7 bullet points or 1-3 short paragraphs.
- Do not provide broad background explanations unless explicitly requested.
- For setup/run questions, first state the current project status, then list only required commands or installs.
- If the project files do not exist yet, say that briefly and avoid describing full future architecture unless asked.
- Use English technical terms as-is, such as OpenGL, CMake, shader, buffer, vertex, constraint, collision, etc.
- Do not make unsupported assumptions.
- If required information is missing, ask the user for that information before proceeding.
- If the information can be checked directly from the project files or command outputs, check it directly instead of guessing.

## Design Discussion Workflow
- When the user asks to design first, discuss structure, or plan the design before implementation, first provide only a high-level list of design items.
- Each item should briefly include:
  - the area or component that may need to change,
  - why the change may be needed,
  - the intended direction of the change.
- In the first design-list response, do not include implementation-level details such as concrete class fields, function bodies, algorithms, file-level diffs, or detailed control flow.
- Discuss design details one item at a time.
- After reflecting an item-level design into `PLAN.md`, return to the next design item discussion unless the user asks to implement.

## Code Modification Rule
- Do not modify code unless the user explicitly asks to change code.
- If the user asks for analysis, explanation, review, or planning, do not edit files.
- When the user asks to use the Git PLAN workflow or PLAN-based version workflow, follow the corresponding skill.
- Meaningful code changes must be based on the current approved `PLAN.md`.
- Before meaningful code changes, write or update `PLAN.md` as the implementation specification for the current version.
- After writing or updating `PLAN.md`, wait for the user's confirmation before applying code changes, unless the user explicitly says to proceed immediately.
- For small local fixes that do not affect design, ask the user whether `PLAN.md` is needed instead of updating it automatically.

## PLAN.md Rule
- `PLAN.md` is the implementation specification for the current version being worked on.
- Keep `PLAN.md` focused on the current active plan.
- Code changes should follow the current approved `PLAN.md`.
- Update `PLAN.md` before continuing implementation when the active plan changes.
- Preserve important discarded or changed decisions only in a short `Rejected / Changed Decisions` section.
- Do not update `PLAN.md` for trivial edits such as comments, formatting, or typo fixes.
- Keep `PLAN.md` specific enough to avoid guessing during implementation.
- Use `Applied baseline` for implemented parts that are still relevant to the current active plan.
- Use `Needs Revision` for implemented parts that must be changed before continuing implementation.
- Use `To Implement Now` for implementation items that should be applied in the current active iteration.
- Use `Blocked` only when implementation cannot continue until missing information, assets, tools, or approval is provided.
- Status markers must not replace detailed implementation specifications.
- Do not remove applied baseline details unless they are no longer relevant, superseded by the current plan, or moved to `Rejected / Changed Decisions`.
- After meaningful code changes, follow `Post-Implementation PLAN Update Rule`.

## Post-Implementation PLAN Update Rule
- After meaningful code changes, update `PLAN.md` before the final response.
- Move completed items out of `To Implement Now` and record them under `Applied baseline` when still relevant.
- Clear or update `Needs Revision` when the revision has been implemented.
- Record actual verification results, such as build success or failure.
- Do not leave already implemented work under `To Implement Now`.

## Recommended PLAN.md Structure
- `Current Goal`: what this version is trying to achieve.
- `Current Scope`: what is included and excluded in this version.
- `Files to Modify`: files expected to be changed and why.
- `Implementation Details`: concrete design and logic to implement.
- `Implementation Status`: optional status tracking for repeated implementation iterations within one active plan.
- `Data Structures / Interfaces`: important classes, functions, buffers, parameters, or APIs.
- `Simulation / Rendering Flow`: relevant update or render sequence.
- `Constraints`: design restrictions, performance concerns, or compatibility requirements.
- `Verification Plan`: how to check whether the change works.
- `Rejected / Changed Decisions`: short notes on important discarded directions.

## PLAN / HISTORY Workflow Rule
- When starting a new meaningful plan in `PLAN.md`, including filling an empty or reset `PLAN.md`, use the `plan-history-workflow` skill.
- Before starting that new plan, read `HISTORY.md` if it exists and use it only as completed-project context.
- Do not treat `HISTORY.md` as the active implementation specification; the active specification is always the current `PLAN.md`.

## Completed PLAN Commit Workflow
1. Follow this workflow when the user explicitly says the current `PLAN.md` is complete and asks to commit and clean up the PLAN.
2. Use both `plan-history-workflow` and `git-plan-workflow` for this workflow: use `plan-history-workflow` for summarizing the completed PLAN into `HISTORY.md`, and use `git-plan-workflow` for Git state checks, staging, commit, remote, and push handling.
3. Check the current `PLAN.md`; if `To Implement Now` or `Needs Revision` still contains remaining work, tell the user the PLAN is not complete and stop this workflow.
4. Summarize the completed PLAN concisely into `HISTORY.md`.
5. Check the Git state.
6. If there is no Git repo, tell the user that repo initialization is required and ask whether to proceed.
7. Check `.gitignore` and make sure build output, temporary files, local-only settings, and external dependencies are not included in the commit.
8. Check `git status` to confirm that the changed files are related to the current PLAN, then stage the changes with `git add .` if there are no unrelated changes.
9. Before committing, suggest multiple commit message options and ask the user to choose or edit one.
10. Commit the code changed during this PLAN as one commit using the user-approved commit message.
11. Connect a remote or push only when the user provides or approves the remote target.

## Design Review Rule
- Before implementing a design, check whether the previous design direction is still valid.
- If the current approach seems to be failing, do not keep forcing the same direction.
- Explicitly consider whether a simpler or different design would solve the problem better.
- Avoid becoming locked into an earlier plan when evidence suggests it is not working.

## Debugging Rule
- When debugging, do not guess causes; first check available evidence such as error messages, compiler output, runtime logs, file contents, or screenshots.
- If evidence is insufficient, ask the user for the specific missing information.
- When suggesting a fix, explain the direct reason briefly.

## Code Style
- Prefer small, incremental changes.
- Avoid rewriting large parts of the project unless explicitly requested.
- Keep simulation logic, rendering logic, input handling, and build configuration reasonably separated.
- Avoid adding new dependencies unless they are necessary and approved.
- Prefer clear names and simple control flow over overly clever code.

## Performance Rule
- Design code with performance in mind from the beginning.
- Prefer simple and efficient implementations.
- Consider runtime cost, memory layout, unnecessary allocations, and unnecessary GPU/CPU synchronization.
- Avoid complex or speculative optimizations that reduce readability without clear evidence.
- For performance-sensitive changes, briefly state the expected performance reason.

## Environment and Tooling Safety
- Do not make persistent environment or tooling changes unless the user explicitly requests and confirms them.
- Persistent changes include installing, uninstalling, upgrading, or downgrading tools, packages, SDKs, drivers, compilers, build tools, package managers, or dependencies.
- Persistent changes also include modifying PATH, environment variables, shell profiles, IDE settings, Git global config, CMake/toolchain settings, or external package-manager state such as vcpkg, Conan, npm, or pip global/user installs.
- Exception: package changes inside the project-local `envs/cloth-sim` environment may be made without additional confirmation when directly required by the approved `PLAN.md`.
- If a package change inside `envs/cloth-sim` causes problems or becomes unnecessary, inspect the environment and revert the package change when possible.
- Before proposing an environment or tooling change, first inspect the current state and explain:
  - what is currently wrong or missing,
  - what change is needed,
  - why it is needed,
  - what files, settings, or system state it may affect,
  - how to verify or revert it.
- Prefer read-only inspection, project-local commands, and reversible project-local changes.
- Temporary per-command environment variables are allowed when needed to run a command, but do not persist them without user confirmation.
- Do not silently change dependency versions, compiler versions, SDK versions, package manager state, or global/user-level settings.

## Build and Verification
- After code changes, check whether the project still builds when possible.
- Prefer running the smallest relevant build or test command instead of full verification when possible.
- If running the build is not possible, clearly say so and explain what the user should run.
- Do not claim that a change works unless it has been verified.

## Local Build / Run
- Use VS Code terminal or shell commands for build/run.
- Build from the directory that contains the target `CMakeLists.txt`.
- Use CMake with vcpkg toolchain:
  `C:\Users\namho\Desktop\cloth_simulation\vcpkg\scripts\buildsystems\vcpkg.cmake`
- Default configure command:
  `cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=C:\Users\namho\Desktop\cloth_simulation\vcpkg\scripts\buildsystems\vcpkg.cmake`
- Default build command:
  `cmake --build build --config Debug`
- Run the generated `.exe` from the build output directory.
- If build/run commands fail, inspect the actual error output and adjust the command or environment instead of guessing.
