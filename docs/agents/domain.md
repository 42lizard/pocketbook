# Domain docs

## Layout

Use a multi-context layout:

- `CONTEXT-MAP.md`: root index of app contexts and shared concerns.
- `apps/hello/CONTEXT.md`: hello app terminology and domain model.
- `apps/readest-sync/CONTEXT.md`: sync app terminology and domain model.
- `apps/<app>/docs/adr/`: decisions scoped to that app.
- `docs/adr/`: shared tooling and cross-app decisions.

The experimental readest-prototype is outside this setup.

## Reading rules

Before exploring code, read `CONTEXT-MAP.md` and the context documents
relevant to the task. Read applicable root and app-specific ADRs.
For cross-app changes, consult every affected context.

If a document is absent, proceed silently. Create domain documents
lazily when terminology or decisions are established; this setup does
not invent domain models or ADRs.

Use the glossary's vocabulary. Identify genuine terminology gaps for
domain-modeling. Explicitly flag proposals that contradict an existing
ADR rather than silently overriding it.
