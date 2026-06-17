# Contributing Guidelines

Thank you for your interest in contributing to our project. Whether it's a bug
report, new feature, correction, or additional documentation, we greatly value
feedback and contributions from our community.

Please read through this document before submitting any issues or pull requests
to ensure we have all the necessary information to effectively respond to your
bug report or contribution.

## Reporting Bugs/Feature Requests

We welcome you to use the GitHub issue tracker to report bugs or suggest
features.

When filing an issue, please check existing open, or recently closed, issues to
make sure somebody else hasn't already reported the issue. Please try to include
as much information as you can. Details like these are incredibly useful:

- A reproducible test case or series of steps
- The version of our code being used
- Any modifications you've made relevant to the bug
- Anything unusual about your environment or deployment

## Contributing via Pull Requests

Contributions via pull requests are much appreciated. Before starting any work,
please:

1. Make sure you are working against the latest source on the _main_ branch.
2. Check existing open, and recently merged, pull requests to make sure someone
   else hasn't addressed the problem already.

The exact workflow depends on whether you are an AWS-internal developer or an
external contributor.

### AWS-internal developers

Internal developers work directly in this repository:

1. Create a branch off `main` prefixed with `dev/`, for example
   `dev/<your-alias>/<short-description>` or `dev/<feature-name>`.
2. Make your change; please focus on the specific change you are contributing.
   If you also reformat all the code, it will be hard for us to focus on your
   change.
3. Ensure local tests pass and confirm the CI UAT run on your `dev/` branch is
   green.
4. Commit using clear commit messages. Start the subject line with a verb (e.g.
   `Add`, `Remove`, `Update`, `Fix`) followed by a short overview. Use the
   commit body to provide additional detail on the change.
5. Open a pull request from your `dev/` branch into `main`, answering any
   default questions in the pull request interface.
6. Pay attention to any automated CI failures reported in the pull request, and
   stay involved in the conversation.

### External contributors

External contributors cannot create branches in this repository. Instead, work
from your own fork:

1. (Recommended) Open an issue describing the bug or feature you want to work
   on, so we can discuss the approach - we would hate for your time to be
   wasted.
2. Once the work is agreed upon, a member of the internal team will create a
   `dev/` branch for the change. Fork the repository and develop in your fork,
   then raise a pull request against that `dev/` branch so the UAT suite runs in
   CI.
3. Focus on the specific change you are contributing, ensure local tests pass,
   and commit using clear commit messages (see format guidance above).
4. Pay attention to any automated CI failures reported in the pull request, and
   stay involved in the conversation.

GitHub provides additional documentation on
[creating a pull request](https://help.github.com/articles/creating-a-pull-request/).

## Finding contributions to work on

Looking at the existing issues is a great way to find something to contribute
on. As our projects, by default, use the default GitHub issue labels
(enhancement/bug/duplicate/help wanted/invalid/question/wontfix), looking at any
'help wanted' issues is a great place to start.

## Code of Conduct

This project has adopted the
[Amazon Open Source Code of Conduct](https://aws.github.io/code-of-conduct). For
more information see the
[Code of Conduct FAQ](https://aws.github.io/code-of-conduct-faq) or contact
opensource-codeofconduct@amazon.com with any additional questions or comments.

## Security issue notifications

If you discover a potential security issue in this project we ask that you
notify AWS/Amazon Security via our
[vulnerability reporting page](http://aws.amazon.com/security/vulnerability-reporting/).
Please do **not** create a public github issue.

## Licensing

See the [LICENSE](../LICENSE) file for our project's licensing. We will ask you
to confirm the licensing of your contribution.
