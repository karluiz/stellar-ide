# Contributing to Stellar

Thank you for helping improve Stellar. Contributions to documentation, design, accessibility, testing, and code are welcome.

The repository is private during early development. Access and contributions are currently by maintainer invitation. The project may become open source later; a license will be added before that release.

## Code of Conduct

Participation is governed by the [Code of Conduct](CODE_OF_CONDUCT.md). Read it before contributing. Report conduct concerns privately to **karluiz@karluiz.com**.

## Start with a clear change

- For a bug, include what you expected, what happened, and steps to reproduce it.
- For a feature or significant visual change, open an issue before implementing it so the scope can be discussed.
- Keep changes focused. Small, independent changes are easier to review and test.
- Do not commit credentials, private workspace data, generated caches, or unrelated build output.

## Local development

The public landing page is a static GitHub Pages site. It has no package installation or build step.

Requirements:

- Git
- A modern browser
- Python 3 for the local preview server

Clone the repository and create a focused branch:

```bash
git clone https://github.com/karluiz/stellar-ide.git
cd stellar-ide
git switch -c my-change
```

Start the local server from the repository root:

```bash
python -m http.server 8000
```

Open `http://localhost:8000` in a browser. Edit the files and refresh to see the result.

### Building the IDE

The native application lives in `src/`, `ui/`, and `tests/` and builds with CMake. The Raven engine is a Git submodule.

```bash
git submodule update --init --recursive
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/raven-ide --workspace /path/to/a/workspace
```

Requirements: CMake 3.20+, a C++20 compiler, SDL2, `glslangValidator`, and a Vulkan driver.

On macOS, install them with Homebrew. MoltenVK and the Vulkan loader provide the driver:

```bash
brew install cmake sdl2 glslang molten-vk vulkan-loader
```

Run the application against a scratch workspace rather than this repository: it creates `.stellar/`, `.agents/`, `endurance/`, and `wiki/` content in the workspace it opens.

macOS-specific window and keyboard handling lives in `src/platform.cpp`. Changes to the header bar, shortcuts, scaling, child processes, or paths need a check on every platform you cannot test locally; say so in the pull request.

## Project layout

- `index.html` contains page content and metadata.
- `styles.css` contains layout, typography, responsive behavior, and motion.
- `assets/` contains local images, fonts, font licenses, and asset provenance.
- `.github/workflows/pages.yml` deploys the site when GitHub Pages is enabled.

## Making changes

- Match the existing HTML and CSS style.
- Preserve semantic headings, meaningful alternative text, keyboard access, visible focus, and reduced-motion behavior.
- Keep the site responsive and readable at desktop and mobile sizes.
- Prefer local assets and avoid adding frameworks or external runtime dependencies for small changes.
- Do not invent product capabilities, releases, downloads, prices, customers, or performance claims.
- Label demonstration content clearly and do not expose private workspace information in screenshots.
- Preserve font and asset licenses. Add provenance for new or replaced imagery and optimize assets for the web.

## Before submitting

Test the result rather than relying only on source inspection:

1. Check the full page at approximately 1440px desktop width and 390px mobile width.
2. Confirm there is no horizontal overflow, clipped content, or overlapping text.
3. Navigate links and interactive elements with a keyboard.
4. Check visible focus and the skip link.
5. Verify reduced-motion behavior in browser settings or developer tools.
6. Confirm all images and fonts load and the browser console has no errors.
7. Run `git diff --check` for whitespace errors in files you changed.

For visual changes, include before-and-after screenshots when useful.

## Pull requests

When opening a pull request:

- Explain the problem and the change, not only the files edited.
- List the checks you performed and any checks you could not perform.
- Link the related issue when one exists.
- Keep unrelated refactors out of the change.
- Be responsive to review feedback and follow the Code of Conduct.

## Security and privacy

Do not open a public issue for a sensitive security or privacy report. Contact **karluiz@karluiz.com** with enough detail to reproduce and assess the issue.

## Licensing

No open-source license has been selected yet. Do not assume that the repository's future visibility or contributions grant redistribution rights. A `LICENSE` file and clear contribution terms will be added before the project becomes public.
