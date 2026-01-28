# Development Guidelines

## CRITICAL: Mandatory Review Checkpoints

**The agent MUST stop and wait for explicit developer approval at every review checkpoint.**

- **DO NOT proceed** to the next step without developer approval
- **DO NOT assume** approval or skip checkpoints
- **DO NOT batch multiple steps** - stop after each checkpoint
- **ALWAYS present** your work and ask: "Please review and approve before I proceed."

Failure to stop at checkpoints defeats the purpose of collaborative development.

---

## 1. Project Structure

- **Ask the developer for the programming language** before proposing modules
- **Divide the project into multiple modules**
- Each module should fit in a few code files
- Implement one module completely before moving to another

### Typical Module Ingredients
- API
- Control flow
- Execution state
- Functions

### Review Checkpoint (MANDATORY)
- **STOP**: Present the module breakdown to the developer for review
- **WAIT**: Get explicit approval before proceeding to development
- **DO NOT** create project.md or start any module until approved
- Developer may add, remove, or reorganize modules

---

## 2. project.md File

Create a `project.md` file **after modules have been defined and approved**. Keep it updated throughout development.

### Contents
- Project overview (inputs, outputs, constraints)
- **Module dependency diagram** (ASCII art showing relationships)
- **Module dependency table** (which module depends on which)
- Module definitions and their responsibilities
- Story placeholders for each module (filled in as you progress)
- Implementation order and dependencies
- File structure

### Maintenance
- Update when adding/modifying modules
- Update Stories as they are written
- Track implementation status per module
- Document any design decisions or changes

---

## 3. Story (Algorithm Documentation)

A "Story" describes the path from **Input → Output**.

### Characteristics
- Written in comments at the top of each code file
- Written in natural language
- Captures the algorithm used in the code
- Different from a Design Doc
- Serves as part of the context for the AI Agent

### Review Checkpoint (MANDATORY)
- **STOP**: Present the Story to the developer for review
- **WAIT**: Get explicit approval before proceeding to Execution State
- **DO NOT** design execution state until Story is approved
- Developer may refine, modify, or rewrite the Story

---

## 4. Execution State Design

Execution state represents the intermediate state your code will use during processing.

### Process
1. Ask AI Agent to:
   - Study the Control Flow
   - Study other similar code for patterns
   - Generate the Execution State
2. Manually refine the Execution State
3. Be prepared for further modifications later as implementation progresses

### Review Checkpoint (MANDATORY)
- **STOP**: Present the Execution State to the developer for review
- **WAIT**: Get explicit approval before proceeding to Function Skeletons
- **DO NOT** generate function skeletons until Execution State is approved
- Developer may add, remove, or modify state variables

---

## 5. Function/Method Skeleton Generation

### Process
1. Ask AI Agent to:
   - Study the Control Flow
   - Study other similar code for patterns
   - Generate skeleton functions/methods
2. Refine function/method names manually

### Review Checkpoint (MANDATORY)
- **STOP**: Present the Function Skeletons to the developer for review
- **WAIT**: Get explicit approval before proceeding to Implementation
- **DO NOT** start implementation until skeletons are approved
- Developer may rename functions, change signatures, or add/remove methods

---

## 6. Implementation Workflow

### Process
1. Ask AI Agent to:
   - Study the Control Flow
   - Implement a few of the skeletons
   - **No more than ~200 lines at a time**
2. Review the generated code
3. Give feedback asking AI Agent to fix problems
4. **Commit to local git repo** once problems are resolved
5. Repeat until all skeletons are implemented

### Review Checkpoint (MANDATORY)
- **STOP**: Present each implementation chunk (~200 lines) for review
- **WAIT**: Get explicit approval before implementing next chunk
- **DO NOT** continue implementing until current chunk is approved
- Developer may request changes, refactoring, or different approaches

---

## 7. Unit Tests

### Process
1. Generate unit tests for the module before moving to the next module
2. Tests should cover:
   - Normal/happy path cases
   - Edge cases
   - Error conditions
3. Review and refine tests
4. Ensure all tests pass
5. **Commit tests to local git repo**

---

## 8. Code Coverage

### Process
1. After writing tests, **always compute code coverage** for the module
2. **Target: 95%+ coverage** for module implementation files
3. Present coverage report to the developer
4. Identify uncovered lines and add tests or justify why they're not covered
5. Defensive code (asserts, error handling for impossible cases) may remain uncovered

### Coverage Guidelines
- 100% coverage is ideal but not always practical
- Uncovered lines should be:
  - Defensive assertions (assert statements)
  - Error paths that can't be triggered through public API
- If coverage is low, add more tests before moving to next module

### Review Checkpoint (MANDATORY)
- **STOP**: Present the code coverage report to the developer
- **WAIT**: Get explicit approval that coverage is acceptable before proceeding
- **DO NOT** move to the next module until coverage is approved
- Developer may request additional tests to improve coverage

---

## Summary Workflow

**Note: Agent MUST stop at every checkpoint marked with ⛔ and wait for approval.**

```
1. Define all modules (responsibilities, dependencies)
       ↓
   ⛔ MANDATORY STOP: Developer approves module breakdown
       ↓
2. Create project.md (overview, modules, file structure)
       ↓
3. For each module:
       ↓
   3a. Write Story (human-driven, natural language, top of file)
       ↓
       ⛔ MANDATORY STOP: Developer approves Story
       ↓
   3b. Design Execution State (AI-assisted, then refined)
       ↓
       ⛔ MANDATORY STOP: Developer approves Execution State
       ↓
   3c. Generate Function Skeletons (AI-assisted, then refined)
       ↓
       ⛔ MANDATORY STOP: Developer approves Skeletons
       ↓
   3d. Implement (~200 lines at a time)
       ↓
       ⛔ MANDATORY STOP: Developer approves each chunk
       ↓
   3e. Repeat step 3d until module complete
       ↓
   3f. Generate unit tests, review, commit
       ↓
   3g. Compute and report code coverage (target 95%+)
       ↓
       ⛔ MANDATORY STOP: Developer approves coverage
       ↓
   3h. Add tests if coverage is insufficient
       ↓
   3i. Update project.md with progress
       ↓
4. Move to next module (repeat step 3)
```

---

## Coding Style

### C++ Style Guide

**All C++ code MUST follow the Google C++ Style Guide:** https://google.github.io/styleguide/cppguide.html

**Key Points:**
- **Naming:**
  - Classes/Structs: `PascalCase` (e.g., `ResourceManager`)
  - Functions/Methods: `PascalCase` (e.g., `GetResource()`)
  - Variables: `snake_case` (e.g., `resource_id`)
  - Member variables: `snake_case_` with trailing underscore (e.g., `mutex_`)
  - Constants: `kPascalCase` (e.g., `kDefaultTimeout`)
  - Enums: `kPascalCase` for values (e.g., `kTokenBucket`)

- **Formatting:**
  - 2-space indentation
  - 80-character line limit (soft limit, 100 hard limit)
  - Opening braces on same line
  - Spaces around operators

- **Headers:**
  - Use `#pragma once` or include guards (`#ifndef`/`#define`/`#endif`)
  - Include what you use (IWYU)
  - Order: related header, C system, C++ standard, other libraries, project headers

- **Comments:**
  - Use `//` for single-line comments
  - Use `/* */` for multi-line comments sparingly
  - Document public APIs with `///` or `/** */` for Doxygen

### Go Style Guide

**All Go code MUST follow the Google Go Style Guide:** https://google.github.io/styleguide/go/

**Key Points:**
- **Naming:**
  - Exported (public): `PascalCase` (e.g., `ResourceManager`, `GetResource`)
  - Unexported (private): `camelCase` (e.g., `resourceManager`, `getResource`)
  - Acronyms: all caps (e.g., `HTTPServer`, `XMLParser`, `ID`)
  - Interfaces: single-method interfaces often end in `-er` (e.g., `Reader`, `Writer`)

- **Formatting:**
  - Use `gofmt` / `goimports` (mandatory, no exceptions)
  - Tabs for indentation
  - No line length limit, but break long lines sensibly

- **Packages:**
  - Package names: lowercase, single word, no underscores (e.g., `throttler`, `ratelimit`)
  - Avoid stutter (e.g., `throttler.Client` not `throttler.ThrottlerClient`)

- **Comments:**
  - Doc comments start with the name being documented (e.g., `// Client represents...`)
  - Complete sentences with proper punctuation
  - Use `//` for all comments

- **Error Handling:**
  - Always check errors
  - Return errors, don't panic
  - Wrap errors with context using `fmt.Errorf("context: %w", err)`
