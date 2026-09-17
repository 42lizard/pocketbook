# Issue tracker: GitHub

Issues and specs live in GitHub Issues for `42lizard/pocketbook`.
Use the `gh` CLI with `--repo 42lizard/pocketbook`.

- Read tickets with `gh issue view <number> --comments`.
- List tickets with `gh issue list`, including relevant labels and state.
- Create tickets with `gh issue create`.
- Add comments with `gh issue comment`.
- Change labels with `gh issue edit`.
- Close resolved tickets with `gh issue close`.

For multiline issue bodies and comments, write the text to a temporary
file and pass `--body-file`.

When a skill says “publish to the issue tracker”, create a GitHub issue.
When it says “fetch the relevant ticket”, read the issue and its comments.

PRs as a request surface: no.
