# GitHub Go Repository Crawler

Searches GitHub for Go repositories that use both:

- [gin-gonic/gin](https://github.com/gin-gonic/gin) (web framework)
- [samber/lo](https://github.com/samber/lo) (utility library)

Results are saved incrementally to a text file (one URL per line).

## Build

```bash
make
```

Requires: `gcc`, `libcurl` (with development headers).

## Usage

```bash
# Without authentication (10 requests/min, may miss results)
./crawler

# With GitHub token (30 requests/min, better results)
./crawler YOUR_GITHUB_TOKEN

# Custom output file
./crawler YOUR_GITHUB_TOKEN results.txt

# Or use environment variable
export GITHUB_TOKEN="ghp_..."
./crawler
```

## How It Works

1. Queries GitHub's Code Search API for `go.mod` files containing both `"samber/lo"` and `"gin-gonic/gin"` in Go repositories.
2. Extracts repository URLs from each result.
3. Deduplicates and appends new URLs to the output file as they are found.
4. Handles pagination (up to 1000 results, GitHub's limit).
5. Respects rate limits with automatic delays and retry on 403/429.

## Output

Results are saved to `repos.txt` (default), one URL per line:

```
https://github.com/user/repo1
https://github.com/user/repo2
...
```

## Notes

- GitHub Code Search limits results to 1000 items maximum.
- Without a token, the search rate is limited to 10 requests per minute.
- A personal access token with no special scopes is sufficient for public repo search.
