// parser.h
// Adapted from HtmlParser by Nicole Hamilton, nham@umich.edu

#pragma once

#include <cstring>
#include "HtmlTags.h"
#include "../../lib/string.h"
#include "../../lib/vector.h"

// This is a simple HTML parser class.  Given a text buffer containing
// a presumed HTML page, the constructor will parse the text to create
// lists of words, title words and outgoing links found on the page.  It
// does not attempt to parse the entire the document structure.
//
// The strategy is to word-break at whitespace and HTML tags and discard
// most HTML tags.  Three tags require discarding everything between
// the opening and closing tag. Five tags require special processing.
//
// We will use the list of possible HTML element names found at
// https://developer.mozilla.org/en-US/docs/Web/HTML/Element +
// !-- (comment), !DOCTYPE and svg, stored as a table in HtmlTags.h.

// Here are the rules for recognizing HTML tags.
//
// 1. An HTML tag starts with either < if it's an opening tag or </ if it's
//    a closing token.  If it starts with < and ends with /> it is both.
//
// 2. The name of the tag must follow the < or </ immediately.  There can't
//    be any whitespace.
//
// 3. The name is terminated by whitespace, > or / and is case-insensitive.
//    The exception is <!--, which starts a comment and is not required
//    to be terminated.
//
// 4. If the name is terminated by whitepace, arbitrary text representing
//    various arguments may follow, terminated by a > or />.
//
// 5. If the name isn't on the list we recognize, we assume it's just
//    ordinary text.
//
// 6. Every token is taken as a word-break.
//
// 7. Most opening or closing tokens can simply be discarded.
//
// 8. <script>, <style>, and <svg> require discarding everything between the
//    opening and closing tag.  Unmatched closing tags are discarded.
//
// 9. <!--, <title>, <a>, <base> and <embed> require special processing.
//
//      <!-- is the beginng of a comment.  Everything up to the ending -->
//          is discarded.
//
//      <title> should cause all the words between the opening and closing
//          tags to be added to the titleWords vector rather than the default
//          words vector.  A closing </title> without an opening <title> is discarded.
//
//      <a> is expected to contain an href="...url..."> argument with the
//          URL inside the double quotes that should be added to the list
//          of links.  All the words in between the opening and closing tags
//          should be collected as anchor text associated with the link
//          in addition to being added to the words or titleWords vector,
//          as appropriate.  A closing </a> without an opening <a> is
//          discarded.
//
//     <base> may contain an href="...url..." parameter.  If present, it should
//          be captured as the base variable.  Only the first is recognized; any
//          others are discarded.
//
//     <embed> may contain a src="...url..." parameter.  If present, it should be
//          added to the links with no anchor text.

// David Note: It seems that if there is no closing tag, we are to treat the next opening
// tag of the same type as a closing tag. (Based on a comment Hamilton made in lecture &
// the examples).

struct Link {
    string URL;
    vector<string> anchorText;

    Link(string url) : URL(static_cast<string&&>(url)) {}
};


class HtmlParser {
public:
    vector<string> words, titleWords;
    vector<Link> links;
    string base;

    // The constructor is given a buffer and length containing
    // presumed HTML.  It will parse the buffer, stripping out
    // all the HTML tags and producing the list of words in body,
    // words in title, and links found on the page.

    HtmlParser(const char *buffer, size_t length) : base("") {
        const char* end = buffer + length;
        const char* p = buffer;
        const char* word_start = p;

        bool in_a = false, in_title = false, base_found = false;
        while (p < end) {
            if (isspace(*p)) {
                if (p > word_start) {
                    size_t word_len = p - word_start;
                    if (in_a) links.back().anchorText.push_back(string(word_start, word_len));

                    if (in_title) titleWords.push_back(string(word_start, word_len));
                    else words.push_back(string(word_start, word_len));
                    word_start = ++p;
                } else {
                    p++;
                    word_start++;
                }
            }

            // Handle potential start of tag
            else if (*p == '<') {
                const char* tag_start = ++p;
                bool is_closing = false;
                if (*p == '/') {
                    // Closing tag
                    is_closing = true;
                    tag_start = ++p;
                }

                // Continue until end of tag name
                while (p < end && !(isspace(*p) || *p == '/' || *p == '>')) p++;

                // If the tag starts with '!', see if it's a comment -- otherwise use length until whitespace
                if (!strncmp(tag_start, "!--", 3)) p = tag_start + 3;
                size_t len = p - tag_start;

                // Lookup tag name
                DesiredAction act = LookupPossibleTag(tag_start, tag_start + len);

                if (act != DesiredAction::OrdinaryText) {
                    // If the tag was immediately after a word, add it to our list
                    if ((!is_closing && word_start < tag_start - 1) || word_start < tag_start - 2) {
                        size_t word_len = is_closing ? (tag_start - 2) - word_start : (tag_start - 1) - word_start;
                        if (in_a) links.back().anchorText.push_back(string(word_start, word_len));

                        if (in_title) titleWords.push_back(string(word_start, word_len));
                        else words.push_back(string(word_start, word_len));
                    }
                }

                switch (act) {
                    case DesiredAction::OrdinaryText:
                        // Treat as normal text -- just advance pointer
                        if (isspace(*p)) {
                            size_t word_len = p - word_start;
                            if (in_a) links.back().anchorText.push_back(string(word_start, word_len));

                            if (in_title) titleWords.push_back(string(word_start, word_len));
                            else words.push_back(string(word_start, word_len));
                            word_start = ++p;
                        } else p++;
                        break;
                    case DesiredAction::Title:
                        // Toggle title state accordingly
                        in_title = !is_closing;
                        while (p < end && *p != '>') p++;
                        word_start = ++p;
                        break;
                    case DesiredAction::Comment:
                        // Advance until we hit a closing tag or the end, ignoring everything in between
                        while (true) {
                            while (p < end && !(*p == '-' && *(p + 1) == '-' && *(p + 2) == '>')) p++;

                            // Found closing tag, advance pointer past it
                            // (In theory this could advance the pointer past the end, but if so we'll exit the outer while, so nbd)
                            p += 3;
                            word_start = p;
                            break;
                        }
                        break;
                    case DesiredAction::Discard:
                        // Skip the tag and advance pointer
                        while (p < end && *p != '>') p++;
                        word_start = ++p;
                        break;
                    case DesiredAction::DiscardSection:
                        // Advance until we hit a closing tag or the end, ignoring everything in between
                        // Loop guard checks whether it was an unmatched closing tag, in which case we just skip
                        while (!is_closing) {
                            while (p < end && !(*p == '<' && *(p + 1) == '/')) p++;
                            if (p == end) break;

                            // Once we've found closing tag, see if it matches
                            p += 2;
                            const char* close_start = p;
                            while (p < end && !(isspace(*p) || *p == '>')) p++;

                            // If closing tag matches the opening tag, we've exited the discard region
                            if (!strncasecmp(close_start, tag_start, len)) break;

                            // If closing tag doesn't match, keep looking for a closing tag
                        }

                        word_start = ++p;
                        break;
                    case DesiredAction::Anchor:
                        if (is_closing) {
                            in_a = false;
                            while (p < end && *p != '>') p++;
                        } else {
                            while (p < end && *p != '>') {
                                if (!strncasecmp(p, "href=\"", 6) && isspace(*(p - 1))) {
                                    const char* a_start = (p += 6);
                                    while (p < end && *p != '"') p++;

                                    if (p != end && p > a_start) {
                                        links.push_back(Link(string(a_start, p - a_start)));
                                        in_a = true;
                                    }
                                } else p++;
                            }
                        }

                        word_start = ++p;
                        break;
                    case DesiredAction::Base:
                        // If already found base link, discard this section
                        if (base_found) {
                            while (p < end && *p != '>') p++;
                        } else {
                            while (p < end && *p != '>') {
                                // Search for href and capture the link
                                if (!strncasecmp(p, "href=\"", 6)) {
                                    const char* base_start = (p += 6);
                                    while (p < end && *p != '"') p++;

                                    base = string(base_start, p - base_start);
                                    base_found = true;
                                } else p++;
                            }
                        }

                        word_start = ++p;
                        break;
                    case DesiredAction::Embed:
                        while (p < end && *p != '>') {
                            // Search for src and capture link
                            if (!strncasecmp(p, "src=\"", 5)) {
                                const char* embed_start = (p += 5);
                                while (p < end && *p != '"') p++;

                                links.push_back(Link(string(embed_start, p - embed_start)));
                            } else p++;
                        }

                        word_start = ++p;
                        break;
                }

            }

            // If normal character, just increment p
            else p++;
        }
    }
};
