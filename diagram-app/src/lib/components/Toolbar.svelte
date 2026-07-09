<script lang="ts">
  import { ui, type Tool } from '$lib/model/scene.svelte';

  interface ToolDef {
    id: Tool;
    label: string;
    glyph: string;
    title: string;
  }

  const groups: { name: string; tools: ToolDef[] }[] = [
    {
      name: '',
      tools: [{ id: 'select', label: 'Select', glyph: '➤', title: 'Select / move (V)' }]
    },
    {
      name: 'Robots',
      tools: [{ id: 'robot', label: 'Robot', glyph: '▤', title: 'Place a robot (R)' }]
    },
    {
      name: 'Lines',
      tools: [
        { id: 'straight', label: 'Line', glyph: '╱', title: 'Straight line (L)' },
        { id: 'arc', label: 'Arc', glyph: '◜', title: 'Arc of a circle' },
        { id: 'spline', label: 'Spline', glyph: '∿', title: 'Spline through points' },
        { id: 'splineHeading', label: 'Spline·H', glyph: '➘', title: 'Spline with fixed start heading' },
        { id: 'boomerang', label: 'Boom', glyph: '↝', title: 'Boomerang path' }
      ]
    },
    {
      name: 'Shapes',
      tools: [
        { id: 'circle', label: 'Circle', glyph: '◯', title: 'Circle' },
        { id: 'rect', label: 'Rect', glyph: '▢', title: 'Rectangle' }
      ]
    },
    {
      name: 'Annotate',
      tools: [{ id: 'text', label: 'Text', glyph: 'T', title: 'Text (T)' }]
    },
    {
      name: 'Stamps',
      tools: [
        { id: 'stamp-goal-blue', label: 'Goal', glyph: '◉', title: 'Blue goal' },
        { id: 'stamp-goal-red', label: 'Goal', glyph: '◉', title: 'Red goal' }
      ]
    }
  ];
</script>

<div class="rail">
  {#each groups as g}
    {#if g.name}<div class="grp">{g.name}</div>{/if}
    {#each g.tools as t}
      <button
        class="tool"
        class:active={ui.tool === t.id}
        class:blue={t.id === 'stamp-goal-blue'}
        class:red={t.id === 'stamp-goal-red'}
        title={t.title}
        onclick={() => (ui.tool = t.id)}
      >
        <span class="glyph">{t.glyph}</span>
        <span class="lbl">{t.label}</span>
      </button>
    {/each}
  {/each}
</div>

<style>
  .rail {
    width: 78px;
    background: var(--panel);
    border-right: 1px solid var(--border);
    display: flex;
    flex-direction: column;
    align-items: stretch;
    padding: 8px 8px 20px;
    gap: 3px;
    overflow-y: auto;
  }
  .grp {
    font-size: 10px;
    text-transform: uppercase;
    letter-spacing: 0.06em;
    color: var(--text-faint);
    margin: 10px 2px 2px;
  }
  .tool {
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: 2px;
    padding: 7px 2px 5px;
    background: transparent;
    border: 1px solid transparent;
    border-radius: var(--radius-sm);
    color: var(--text-dim);
  }
  .tool:hover {
    background: var(--panel-2);
    color: var(--text);
  }
  .tool.active {
    background: var(--accent-soft);
    border-color: var(--accent);
    color: var(--text);
  }
  .glyph {
    font-size: 17px;
    line-height: 1;
  }
  .tool.blue .glyph {
    color: var(--c-blue);
  }
  .tool.red .glyph {
    color: var(--c-red);
  }
  .lbl {
    font-size: 10px;
  }
</style>
