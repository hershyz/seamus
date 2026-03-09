// parser.h
// Adapted from HtmlParser by Nicole Hamilton, nham@umich.edu

#pragma once

#include <cstring>

#include "word_array.h"
// #include "../../lib/string.h"
// #include "../../lib/vector.h"
#include <string>
#include <vector>
#include "../../lib/buffer.h"

#include "HtmlTags.h"

using std::string;
using std::vector;


// Store URL + anchor text
struct Link {
    string URL;
    vector<string> anchorText;

    Link(string url)
        : URL(static_cast<string &&>(url)) {}
};


class HtmlParser {
public:
    static constexpr uint32_t MAX_LINK_VECSIZE = 32 * 1024;

    int in_fd_;
    buffer buf;
    word_array words;
    vector<Link> links;
    string base;

    HtmlParser(int in_fd, int out_fd)
        : in_fd_(in_fd), words(out_fd) {}

    // Reads a chunk from the input fd, parses it, and shifts unconsumed bytes.
    // Returns bytes read, 0 on EOF, -1 on error.
    ssize_t read_and_parse() {
        ssize_t status = buf.read(in_fd_);
        if (status <= 0) return status;

        size_t consumed = parse_buf();
        buf.shift_data(consumed);
        return status;
    }

    // Call after read_and_parse() returns 0. Parses remaining bytes and flushes words.
    void finish() {
        if (buf.size() > 0) {
            const char *data = buf.data();
            const char *end = data + buf.size();
            const char *parse_start = data;

            if (in_comment_) {
                in_comment_ = false;
                buf.clear();
                words.flush();
                return;
            }
            if (in_discard_) {
                const char *p = data;
                while (p < end) {
                    if (*p == '<' && p + 1 < end && *(p + 1) == '/') {
                        const char *close_name = p + 2;
                        const char *close_p = close_name;
                        while (close_p < end && *close_p != '>' && !isspace(*close_p)) close_p++;
                        if (close_p < end && (size_t) (close_p - close_name) == discard_tag_len_
                            && !strncasecmp(close_name, discard_tag_, discard_tag_len_)) {
                            while (close_p < end && *close_p != '>') close_p++;
                            in_discard_ = false;
                            parse_start = (close_p < end) ? close_p + 1 : end;
                            break;
                        }
                    }
                    p++;
                }
                if (in_discard_) {
                    in_discard_ = false;
                    buf.clear();
                    words.flush();
                    return;
                }
            }

            parse_chunk(parse_start, end - parse_start);
            buf.clear();
        }
        words.flush();
    }

private:
    // Internal parse that operates on the current buffer contents
    size_t parse_buf() {
        const char *data = buf.data();         // Front of buffer
        const char *end = data + buf.size();   // End of buffer
        // Where parse_chunk should start (may be larger than data,
        // because the front of the buffer may have remnants from last chunk)
        const char *parse_start = data;

        // If continuing a comment from previous chunk, scan for -->
        if (in_comment_) {
            const char *p = data;
            while (p + 2 < end) {
                if (*p == '-' && *(p + 1) == '-' && *(p + 2) == '>') {
                    in_comment_ = false;

                    // Tell parse_chunk to ignore the comment
                    parse_start = p + 3;
                    break;
                }
                p++;
            }
            if (in_comment_) {
                // Entire chunk is still inside comment. Consume everything
                // except last 4 bytes, in case --> is cut off again.
                return (buf.size() >= 4) ? buf.size() - 4 : 0;
            }
        }

        // If continuing a discard section from previous chunk, scan for </tag>
        if (in_discard_) {
            const char *p = data;
            while (p < end) {
                if (*p == '<' && p + 1 < end && *(p + 1) == '/') {
                    const char *close_name = p + 2;
                    const char *close_p = close_name;
                    while (close_p < end && *close_p != '>' && !isspace(*close_p)) close_p++;
                    if (close_p < end && (size_t) (close_p - close_name) == discard_tag_len_
                        && !strncasecmp(close_name, discard_tag_, discard_tag_len_)) {
                        // Found matching close tag, skip past >
                        while (close_p < end && *close_p != '>') close_p++;
                        in_discard_ = false;

                        // Tell parse_chunk to ignore the discard area
                        parse_start = (close_p < end) ? close_p + 1 : end;
                        break;
                    }
                }
                p++;
            }
            if (in_discard_) {
                // Entire chunk is still inside discard section. Consume everything
                // except last 12 bytes (max closing tag: </script> is 9 bytes).
                return (buf.size() >= 12) ? buf.size() - 12 : 0;
            }
        }

        // Scan backwards until finding the first '>' to avoid splitting a tag, which kinda screws us
        const char *safe_end = end;
        {
            const char *scan = end;
            while (scan > parse_start && *(scan - 1) != '>') scan--;
            if (scan > parse_start) safe_end = scan;
            // If no '>' found after parse_start, parse everything (plain text chunk)
        }

        // Parse region after fixing any split-buffer issues
        parse_chunk(parse_start, safe_end - parse_start);

        // Return how many bytes from the front of the buffer were consumed
        return safe_end - data;

        /* Example

            buffer: [ello World! --> Hello World! </div> <d]

            1. parser sees we're in comment (fromt last buffer), so it goes until finding
            '-->'. It sets parse_start to the ' ' after '-->'. That way the parse_chunk
            ignore the residual comment.

            2. parse finds safe_end at the '>' in </div>. This 'reserves' the
            <d at the end of the buffer which will be saved and processed in the next parse.
            This way, we don't try to parse the <d which would be catastrophic as the
            next buffer would start with 'iv/>'.

            3. parse calls parse_chunk(parse_start, safe_end - parse_start), which
            ends up passing ' Hello World! </div> '. This is safe to parse.


        */
    }

    // Persistent state
    bool in_a_ = false;
    bool in_title_ = false;
    bool base_found_ = false;

    // Needed for multi-chunk comments and discard sections
    bool in_comment_ = false;
    bool in_discard_ = false;
    char discard_tag_[16] = {};
    size_t discard_tag_len_ = 0;

    // Core parsing logic, man i'm glad David basically already did this
    void parse_chunk(const char *buffer, size_t length) {
        const char *end = buffer + length;
        const char *p = buffer;
        const char *word_start = p;

        // Parse <length> bytes starting at <buffer>
        while (p < end) {
            if (isspace(*p)) {
                if (p > word_start) {
                    size_t word_len = p - word_start;
                    if (in_a_) links.back().anchorText.push_back(string(word_start, word_len));

                    words.push_back(word_start, word_len);
                    word_start = ++p;
                } else {
                    p++;
                    word_start++;
                }
            }

            // Handle potential start of tag
            else if (*p == '<') {
                const char *tag_start = ++p;
                bool is_closing = false;
                if (p < end && *p == '/') {
                    // Closing tag
                    is_closing = true;
                    tag_start = ++p;
                }

                // Continue until end of tag name
                while (p < end && !(isspace(*p) || *p == '/' || *p == '>')) p++;

                // If the tag starts with '!', see if it's a comment, otherwise use length until whitespace
                if (tag_start + 3 <= end && !strncmp(tag_start, "!--", 3)) p = tag_start + 3;
                size_t len = p - tag_start;

                // Lookup tag name
                DesiredAction act = LookupPossibleTag(tag_start, tag_start + len);

                if (act != DesiredAction::OrdinaryText) {
                    // If the tag was immediately after a word, add it to our list
                    if ((!is_closing && word_start < tag_start - 1) || word_start < tag_start - 2) {
                        size_t word_len = is_closing ? (tag_start - 2) - word_start : (tag_start - 1) - word_start;
                        if (in_a_) links.back().anchorText.push_back(string(word_start, word_len));

                        words.push_back(word_start, word_len);
                    }
                }

                switch (act) {
                case DesiredAction::OrdinaryText:
                    if (p < end && isspace(*p)) {
                        size_t word_len = p - word_start;
                        if (in_a_) links.back().anchorText.push_back(string(word_start, word_len));


                        words.push_back(word_start, word_len);
                        word_start = ++p;
                    } else if (p < end)
                        p++;
                    break;
                case DesiredAction::Title:
                    // Toggle title state accordingly
                    in_title_ = !is_closing;
                    if (in_title_) {
                        words.push_back("<title>", 7);
                    } else {
                        words.push_back("<\\title>", 8);
                    }

                    while (p < end && *p != '>') p++;
                    if (p < end) word_start = ++p;
                    break;
                case DesiredAction::Comment:
                    // Scan for closing -->
                    while (p + 2 < end && !(*p == '-' && *(p + 1) == '-' && *(p + 2) == '>')) p++;
                    if (p + 2 >= end) {
                        // Comment spans into next chunk
                        in_comment_ = true;
                        return;
                    }
                    p += 3;
                    word_start = p;
                    break;
                case DesiredAction::Discard:
                    // Skip the tag and advance pointer
                    while (p < end && *p != '>') p++;
                    if (p < end) word_start = ++p;
                    break;
                case DesiredAction::DiscardSection:
                    // Advance until we hit a closing tag or the end, ignoring everything in between
                    // Loop guard checks whether it was an unmatched closing tag, in which case we just skip
                    while (!is_closing) {
                        while (p + 1 < end && !(*p == '<' && *(p + 1) == '/')) p++;
                        if (p + 1 >= end) {
                            // Discard section spans into next chunk
                            in_discard_ = true;
                            discard_tag_len_ = (len < sizeof(discard_tag_)) ? len : sizeof(discard_tag_) - 1;
                            memcpy(discard_tag_, tag_start, discard_tag_len_);
                            return;
                        }

                        // Once we've found closing tag, see if it matches
                        p += 2;
                        const char *close_start = p;
                        while (p < end && !(isspace(*p) || *p == '>')) p++;

                        // If closing tag matches the opening tag, we've exited the discard region
                        if (!strncasecmp(close_start, tag_start, len)) break;

                        // If closing tag doesn't match, keep looking for a closing tag
                    }

                    if (!in_discard_) {
                        while (p < end && *p != '>') p++;
                        if (p < end) word_start = ++p;
                    }
                    break;
                case DesiredAction::Anchor:
                    if (is_closing) {
                        in_a_ = false;
                        while (p < end && *p != '>') p++;
                    } else {
                        while (p < end && *p != '>') {
                            if (!strncasecmp(p, "href=\"", 6) && isspace(*(p - 1))) {
                                const char *a_start = (p += 6);
                                while (p < end && *p != '"') p++;

                                if (p != end && p > a_start) {
                                    links.push_back(Link(string(a_start, p - a_start)));
                                    in_a_ = true;
                                }
                            } else
                                p++;
                        }
                    }

                    if (p < end) word_start = ++p;
                    break;
                case DesiredAction::Base:
                    // If already found base link, discard this section
                    if (base_found_) {
                        while (p < end && *p != '>') p++;
                    } else {
                        while (p < end && *p != '>') {
                            // Search for href and capture the link
                            if (!strncasecmp(p, "href=\"", 6)) {
                                const char *base_start = (p += 6);
                                while (p < end && *p != '"') p++;

                                base = string(base_start, p - base_start);
                                base_found_ = true;
                            } else
                                p++;
                        }
                    }

                    if (p < end) word_start = ++p;
                    break;
                case DesiredAction::Embed:
                    while (p < end && *p != '>') {
                        // Search for src and capture link
                        if (!strncasecmp(p, "src=\"", 5)) {
                            const char *embed_start = (p += 5);
                            while (p < end && *p != '"') p++;

                            links.push_back(Link(string(embed_start, p - embed_start)));
                        } else
                            p++;
                    }

                    if (p < end) word_start = ++p;
                    break;
                }

            }

            // If normal character, just increment p
            else
                p++;
        }
    }
};
