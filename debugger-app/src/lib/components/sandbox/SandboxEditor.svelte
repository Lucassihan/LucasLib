<script lang="ts">
  // Controlled contenteditable code editor: VS Code-style syntax coloring plus
  // inline parameter-name inlay hints. The plain-text model (`value`) is the
  // single source of truth — every mutation is intercepted (beforeinput / paste
  // / Tab), applied to the string, and the DOM is rebuilt from scratch with the
  // caret restored by character offset. Inlay spans are contenteditable=false
  // and skipped in every offset calculation, so they never affect the text.

  import { highlightSpans, inlayHints } from '$lib/sandbox/highlight';
  import type { ParseResult } from '$lib/sandbox/parse';

  let {
    value,
    parsed,
    oninput,
    oncaret,
    highlightLine = null
  }: {
    value: string;
    parsed: ParseResult;
    oninput: (v: string) => void;
    oncaret?: (pos: number) => void;
    /** 1-based line to highlight (e.g. the movement selected on the field) */
    highlightLine?: number | null;
  } = $props();

  let el: HTMLDivElement | undefined = $state();
  let shell: HTMLDivElement | undefined = $state();
  let focused = $state(false);
  const lineCount = $derived(value.split('\n').length);

  // Editor line metrics (font-size 11px · line-height 1.55) — used to place the
  // selection band and scroll a field-selected line into view.
  const LINE_PX = 11 * 1.55;
  const PAD_TOP = 10;

  // When a movement is selected on the field (not by typing), pull its line
  // into view so the highlight is visible.
  $effect(() => {
    const ln = highlightLine;
    if (ln === null || !shell || focused) return;
    const top = PAD_TOP + (ln - 1) * LINE_PX;
    const bottom = top + LINE_PX;
    if (top < shell.scrollTop + 8) shell.scrollTop = Math.max(0, top - 24);
    else if (bottom > shell.scrollTop + shell.clientHeight - 8) shell.scrollTop = bottom - shell.clientHeight + 24;
  });
  let sel = $state({ start: 0, end: 0 }); // model selection in char offsets
  let pendingCaret: number | null = null; // caret to restore after next render
  let composing = false;
  let lastHtml = '';

  const esc = (s: string) => s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');

  // DOM string: colored token spans with inlay hints spliced in at arg starts.
  const html = $derived.by(() => {
    const spans = highlightSpans(value);
    const hints = inlayHints(parsed);
    let out = '';
    let hi = 0;
    for (const sp of spans) {
      while (hi < hints.length && hints[hi].offset <= sp.start) {
        out += `<span class="inlay" contenteditable="false">${esc(hints[hi].text)}:</span>`;
        hi++;
      }
      out += `<span class="tok ${sp.cls}">${esc(value.slice(sp.start, sp.end))}</span>`;
    }
    while (hi < hints.length) {
      out += `<span class="inlay" contenteditable="false">${esc(hints[hi].text)}:</span>`;
      hi++;
    }
    // trailing <br> so a final blank line is visible + caret-able
    if (value === '' || value.endsWith('\n')) out += '<br>';
    return out;
  });

  // Rebuild the DOM whenever the highlighted HTML changes, then restore caret.
  $effect(() => {
    const h = html;
    if (!el || h === lastHtml) return;
    lastHtml = h;
    el.innerHTML = h;
    if (pendingCaret !== null) {
      setCaret(pendingCaret, pendingCaret);
      pendingCaret = null;
    } else if (document.activeElement === el) {
      setCaret(sel.start, sel.end);
    }
  });

  // --- offset <-> DOM mapping (inlay text nodes excluded) ---

  function textWalker(): TreeWalker {
    return document.createTreeWalker(el!, NodeFilter.SHOW_TEXT, {
      acceptNode: (t) =>
        (t.parentElement && t.parentElement.closest('.inlay'))
          ? NodeFilter.FILTER_REJECT
          : NodeFilter.FILTER_ACCEPT
    });
  }

  /** char index of a DOM boundary (node, offset), skipping inlay text. */
  function pointToIndex(node: Node | null, offset: number): number | null {
    if (!el || !node || !el.contains(node)) return null;
    const range = document.createRange();
    range.setStart(el, 0);
    try {
      range.setEnd(node, offset);
    } catch {
      return null;
    }
    let count = 0;
    const w = textWalker();
    let t: Node | null;
    while ((t = w.nextNode())) {
      const tn = t as Text;
      let startAfter: number;
      try {
        startAfter = range.comparePoint(tn, 0);
      } catch {
        continue;
      }
      if (startAfter > 0) break; // node begins after the boundary
      let endCmp: number;
      try {
        endCmp = range.comparePoint(tn, tn.length);
      } catch {
        endCmp = 1;
      }
      if (endCmp <= 0) count += tn.length;
      else {
        count += tn === range.endContainer ? range.endOffset : 0;
        break;
      }
    }
    return count;
  }

  /** locate the text node + offset for a char index. */
  function locate(index: number): { node: Node; offset: number } {
    const w = textWalker();
    let cum = 0;
    let last: Text | null = null;
    let t: Node | null;
    while ((t = w.nextNode())) {
      const tn = t as Text;
      last = tn;
      if (cum + tn.length >= index) return { node: tn, offset: Math.max(0, index - cum) };
      cum += tn.length;
    }
    if (last) return { node: last, offset: last.length };
    return { node: el!, offset: 0 };
  }

  function setCaret(start: number, end: number) {
    if (!el) return;
    const a = locate(start);
    const b = locate(end);
    const s = window.getSelection();
    if (!s) return;
    const range = document.createRange();
    range.setStart(a.node, a.offset);
    range.setEnd(b.node, b.offset);
    s.removeAllRanges();
    s.addRange(range);
  }

  function readSelection(): { start: number; end: number; caret: number } | null {
    const s = window.getSelection();
    if (!s || s.rangeCount === 0 || !el) return null;
    const focus = pointToIndex(s.focusNode, s.focusOffset);
    const anchor = pointToIndex(s.anchorNode, s.anchorOffset);
    if (focus === null || anchor === null) return null;
    return { start: Math.min(anchor, focus), end: Math.max(anchor, focus), caret: focus };
  }

  function onSelectionChange() {
    if (composing) return;
    if (!el || document.activeElement !== el) return;
    const r = readSelection();
    if (!r) return;
    sel = { start: r.start, end: r.end };
    oncaret?.(r.caret);
  }

  // --- controlled editing ---

  function applyEdit(nextValue: string, caret: number) {
    if (nextValue === value) return;
    pendingCaret = caret;
    sel = { start: caret, end: caret };
    oninput(nextValue);
    oncaret?.(caret);
  }

  function wordBack(v: string, i: number): number {
    let j = i;
    while (j > 0 && /\s/.test(v[j - 1])) j--;
    while (j > 0 && /\w/.test(v[j - 1])) j--;
    if (j === i && i > 0) j = i - 1;
    return j;
  }
  function wordFwd(v: string, i: number): number {
    let j = i;
    while (j < v.length && /\s/.test(v[j])) j++;
    while (j < v.length && /\w/.test(v[j])) j++;
    if (j === i && i < v.length) j = i + 1;
    return j;
  }

  function onBeforeInput(e: InputEvent) {
    const t = e.inputType;
    if (t === 'insertCompositionText' || t === 'deleteCompositionText') return; // IME
    e.preventDefault();
    const { start, end } = sel;
    const before = value.slice(0, start);
    const after = value.slice(end);
    switch (t) {
      case 'insertText': {
        const d = e.data ?? '';
        applyEdit(before + d + after, start + d.length);
        break;
      }
      case 'insertLineBreak':
      case 'insertParagraph': {
        const lineStart = value.lastIndexOf('\n', start - 1) + 1;
        const indent = /^[ \t]*/.exec(value.slice(lineStart, start))?.[0] ?? '';
        const ins = '\n' + indent;
        applyEdit(before + ins + after, start + ins.length);
        break;
      }
      case 'deleteContentBackward': {
        if (start !== end) applyEdit(before + after, start);
        else if (start > 0) applyEdit(value.slice(0, start - 1) + after, start - 1);
        break;
      }
      case 'deleteContentForward': {
        if (start !== end) applyEdit(before + after, start);
        else if (end < value.length) applyEdit(before + value.slice(end + 1), start);
        break;
      }
      case 'deleteWordBackward': {
        if (start !== end) applyEdit(before + after, start);
        else {
          const j = wordBack(value, start);
          applyEdit(value.slice(0, j) + after, j);
        }
        break;
      }
      case 'deleteWordForward': {
        if (start !== end) applyEdit(before + after, start);
        else {
          const j = wordFwd(value, end);
          applyEdit(before + value.slice(j), start);
        }
        break;
      }
      case 'deleteByCut':
      case 'deleteByDrag':
        applyEdit(before + after, start);
        break;
      // paste/drop handled by the paste event; block the native path here
      default:
        break;
    }
  }

  function onPaste(e: ClipboardEvent) {
    e.preventDefault();
    const d = (e.clipboardData?.getData('text') ?? '').replace(/\r\n?/g, '\n');
    if (!d) return;
    applyEdit(value.slice(0, sel.start) + d + value.slice(sel.end), sel.start + d.length);
  }

  function onKeydown(e: KeyboardEvent) {
    if (e.key === 'Tab') {
      e.preventDefault();
      const ins = '  ';
      applyEdit(value.slice(0, sel.start) + ins + value.slice(sel.end), sel.start + ins.length);
    }
  }

  function onCompositionStart() {
    composing = true;
  }
  function onCompositionEnd() {
    composing = false;
    // read back what the IME committed (skipping inlay), then re-sync the model
    if (!el) return;
    const w = textWalker();
    let text = '';
    let t: Node | null;
    while ((t = w.nextNode())) text += (t as Text).data;
    const r = readSelection();
    lastHtml = ''; // force a rebuild
    applyEdit(text, r ? r.caret : text.length);
  }
</script>

<svelte:document onselectionchange={onSelectionChange} />

<div class="shell" class:focused={focused} bind:this={shell}>
  {#if highlightLine !== null}
    <div class="line-hl" style="top: {PAD_TOP + (highlightLine - 1) * LINE_PX}px; height: {LINE_PX}px" aria-hidden="true"></div>
  {/if}
  <div class="gutter" aria-hidden="true">
    {#each { length: lineCount } as _, i}
      <div class="ln" class:active={i + 1 === highlightLine}>{i + 1}</div>
    {/each}
  </div>
  <!-- svelte-ignore a11y_no_static_element_interactions -->
  <div
    bind:this={el}
    class="editor"
    contenteditable="true"
    spellcheck="false"
    autocapitalize="off"
    role="textbox"
    aria-multiline="true"
    aria-label="Route code"
    onbeforeinput={onBeforeInput}
    onpaste={onPaste}
    onkeydown={onKeydown}
    oncompositionstart={onCompositionStart}
    oncompositionend={onCompositionEnd}
    onfocus={() => (focused = true)}
    onblur={() => (focused = false)}
  ></div>
</div>

<style>
  .shell {
    position: relative;
    flex: 1;
    min-height: 0;
    display: flex;
    align-items: flex-start;
    overflow: auto;
    background: var(--bg-base);
    border-radius: var(--radius-small);
    box-shadow: var(--ring);
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: 11px;
    line-height: 1.55;
    tab-size: 2;
  }

  /* selection band for the movement clicked on the field */
  .line-hl {
    position: absolute;
    left: 0;
    right: 0;
    z-index: 0;
    background: rgba(236, 216, 143, 0.1);
    box-shadow: inset 2px 0 0 rgba(236, 216, 143, 0.55);
    pointer-events: none;
  }
  .shell.focused {
    box-shadow:
      var(--ring),
      0 0 0 1px rgba(255, 255, 255, 0.14);
  }

  /* line-number gutter — sticky so it survives horizontal scroll */
  .gutter {
    position: sticky;
    left: 0;
    z-index: 1;
    flex-shrink: 0;
    min-height: 100%;
    padding: 10px 8px 10px 10px;
    background: var(--bg-base);
    border-right: 1px solid rgba(255, 255, 255, 0.05);
    color: #5a5a5a;
    text-align: right;
    user-select: none;
    -webkit-user-select: none;
    font-variant-numeric: tabular-nums;
  }
  .ln {
    min-width: 18px;
  }
  .ln.active {
    color: #ecd88f;
    font-weight: 700;
  }

  .editor {
    position: relative;
    z-index: 1;
    flex: 1 0 auto;
    color: #d4d4d4;
    padding: 10px 12px;
    outline: none;
    white-space: pre;
    caret-color: #d4d4d4;
    min-height: 100%;
  }

  /* VS Code Dark+ palette */
  .editor :global(.tok.comment) {
    color: #6a9955;
    font-style: italic;
  }
  .editor :global(.tok.number) {
    color: #b5cea8;
  }
  .editor :global(.tok.type) {
    color: #4ec9b0;
  }
  .editor :global(.tok.fn) {
    color: #dcdcaa;
  }
  .editor :global(.tok.prop) {
    color: #9cdcfe;
  }
  .editor :global(.tok.bool),
  .editor :global(.tok.const) {
    color: #569cd6;
  }
  .editor :global(.tok.ident) {
    color: #9cdcfe;
  }
  .editor :global(.tok.punct) {
    color: #d4d4d4;
  }
  .editor :global(.tok.directive) {
    color: #c586c0;
    font-weight: 600;
  }

  /* inlay hint chip — non-editable, non-selectable */
  .editor :global(.inlay) {
    color: #8c8c8c;
    background: rgba(255, 255, 255, 0.06);
    border-radius: 4px;
    padding: 0 3px;
    margin-right: 1px;
    font-size: 9.5px;
    user-select: none;
    -webkit-user-select: none;
    cursor: default;
  }
</style>
