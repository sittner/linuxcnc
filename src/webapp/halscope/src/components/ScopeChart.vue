<script setup lang="ts">
import { ref, watch, onMounted, onBeforeUnmount } from 'vue';
import uPlot from 'uplot';
import 'uplot/dist/uPlot.min.css';
import { scopeStore } from '../stores/scope';
import { ScopeState } from '../generated/halscope_client';

const chartEl = ref<HTMLDivElement>();
let plot: uPlot | null = null;
let resizeObs: ResizeObserver | null = null;

const NUM_DIVS = 10; // 10 vertical divisions like original scope

/**
 * Auto-detect a nice 1-2-5 scale for a channel's data range.
 * Returns units-per-division.
 */
function autoScale(data: Float64Array): number {
  if (data.length === 0) return 1;
  let min = Infinity, max = -Infinity;
  for (let i = 0; i < data.length; i++) {
    if (data[i] < min) min = data[i];
    if (data[i] > max) max = data[i];
  }
  let range = max - min;
  if (range === 0) range = Math.abs(max) || 1;
  // Target: data fills ~80% of the display (8 of 10 divs)
  const target = range / 8;
  // Find nearest 1-2-5 value
  const exp = Math.floor(Math.log10(target));
  const base = Math.pow(10, exp);
  const norm = target / base;
  let scale: number;
  if (norm <= 1) scale = base;
  else if (norm <= 2) scale = 2 * base;
  else if (norm <= 5) scale = 5 * base;
  else scale = 10 * base;
  return scale || 1;
}

/**
 * Auto-detect center offset for a channel (in divisions).
 * Centers the data midpoint at division 0.
 */
function autoOffset(data: Float64Array, vScale: number): number {
  if (data.length === 0) return 0;
  let min = Infinity, max = -Infinity;
  for (let i = 0; i < data.length; i++) {
    if (data[i] < min) min = data[i];
    if (data[i] > max) max = data[i];
  }
  const mid = (min + max) / 2;
  // Return offset in divisions that would center the data
  return -(mid / vScale);
}

/** Ensure channel has a reasonable initial scale from sample data */
function ensureChannelScale(chIdx: number) {
  const ui = scopeStore.channelUI[chIdx];
  if (!ui || ui.scaleSet) return; // already set
  const s = scopeStore.state.samples.find(s => s.channel === chIdx);
  if (!s || s.data.length === 0) return;
  ui.vScale = autoScale(s.data);
  ui.vOffset = autoOffset(s.data, ui.vScale);
  ui.scaleSet = true;
}

function buildOpts(width: number, height: number): uPlot.Options {
  const channels = scopeStore.state.status.channels.filter(c => c.enabled);
  const selCh = scopeStore.state.selectedChannel;
  const selUI = selCh >= 0 ? scopeStore.channelUI[selCh] : null;

  const series: uPlot.Series[] = [
    { label: 'Time (s)' },
    ...channels.map((ch) => ({
      label: ch.pinName || `Ch ${ch.channel}`,
      stroke: scopeStore.channelUI[ch.channel]?.color ?? '#ffff00',
      width: ch.channel === selCh ? 2 : 1,
      show: scopeStore.channelUI[ch.channel]?.visible ?? true,
    })),
  ];

  // Y axis: -5 to +5 divisions (fixed, like original scope)
  const halfDivs = NUM_DIVS / 2;

  return {
    width,
    height,
    cursor: { drag: { x: true, y: true } },
    scales: {
      x: { time: false },
      y: {
        auto: false,
        range: [-halfDivs, halfDivs],
      },
    },
    axes: [
      {
        label: 'Time (s)',
        stroke: '#888',
        grid: { stroke: '#333', width: 1 },
        ticks: { stroke: '#444', width: 1 },
      },
      {
        stroke: '#888',
        grid: { stroke: '#333', width: 1 },
        ticks: { stroke: '#444', width: 1 },
        // Label Y axis ticks in the selected channel's real units
        values: (_self: uPlot, divs: number[]) => {
          if (!selUI) return divs.map(d => d.toFixed(1));
          return divs.map(d => {
            const real = (d - selUI.vOffset) * selUI.vScale;
            // Format nicely
            if (Math.abs(real) >= 1000) return (real / 1000).toFixed(1) + 'k';
            if (Math.abs(real) >= 1) return real.toFixed(2);
            if (Math.abs(real) >= 0.001) return (real * 1000).toFixed(1) + 'm';
            return real.toExponential(1);
          });
        },
      },
    ],
    series,
  };
}

function buildData(): uPlot.AlignedData {
  const st = scopeStore.state;
  if (st.timeBase.length === 0 || st.samples.length === 0) {
    return [new Float64Array(0)];
  }

  const time = Array.from(st.timeBase) as unknown as number[];
  const data: (number[] | Float64Array)[] = [time];

  const channels = st.status.channels.filter(c => c.enabled);
  for (const ch of channels) {
    const s = st.samples.find(s => s.channel === ch.channel);
    const ui = scopeStore.channelUI[ch.channel];
    if (s && ui) {
      // Transform to division space: div = (rawValue / vScale) + vOffset
      const transformed = new Float64Array(s.data.length);
      const scale = ui.vScale || 1;
      const offset = ui.vOffset;
      for (let i = 0; i < s.data.length; i++) {
        transformed[i] = (s.data[i] / scale) + offset;
      }
      data.push(transformed);
    } else {
      data.push(new Float64Array(st.timeBase.length));
    }
  }

  return data as uPlot.AlignedData;
}

/** Apply the display window X range to the chart via uPlot scales */
function applyViewWindow() {
  if (!plot) return;
  const dw = scopeStore.calcDisplayWindow();
  plot.setScale('x', { min: dw.screenStartTime, max: dw.screenEndTime });
}

function createPlot() {
  if (!chartEl.value) return;
  plot?.destroy();

  const rect = chartEl.value.getBoundingClientRect();
  const w = Math.max(rect.width, 200);
  const h = Math.max(rect.height, 150);

  const opts = buildOpts(w, h);
  const data = buildData();
  plot = new uPlot(opts, data, chartEl.value);
  applyViewWindow();

  // Add scroll-to-zoom on the chart (like original scope_disp.c change_zoom)
  plot.over.addEventListener('wheel', (e: WheelEvent) => {
    e.preventDefault();
    const dir = e.deltaY < 0 ? 1 : -1;
    scopeStore.setHorizZoom(scopeStore.state.zoomSetting + dir);
  });
}

function updateData() {
  if (!plot) {
    createPlot();
    return;
  }

  // Auto-detect scale for channels that haven't been user-adjusted
  const channels = scopeStore.state.status.channels.filter(c => c.enabled);
  for (const ch of channels) {
    ensureChannelScale(ch.channel);
  }

  const data = buildData();

  // If channel count changed, rebuild the plot (series config differs)
  if (channels.length + 1 !== plot.series.length) {
    createPlot();
    return;
  }

  plot.setData(data);
  applyViewWindow();
}

// Watch for sample data changes
watch(
  () => scopeStore.state.samples,
  () => updateData(),
);

// Watch for view window changes (pan/zoom) — just update X scale, not rebuild
watch(
  () => [scopeStore.state.zoomSetting, scopeStore.state.posSetting],
  () => applyViewWindow(),
);

// Watch for selected channel or channelUI changes — rebuild plot for Y axis labels
watch(
  () => scopeStore.state.selectedChannel,
  () => createPlot(),
);

// Watch for per-channel scale/offset changes — update data transform
watch(
  () => {
    const sel = scopeStore.state.selectedChannel;
    if (sel < 0) return null;
    const ui = scopeStore.channelUI[sel];
    return ui ? `${ui.vScale}:${ui.vOffset}` : null;
  },
  () => {
    if (plot) {
      plot.setData(buildData());
      // Rebuild to update Y axis label values
      createPlot();
    }
  },
);

// Watch for status changes (channel list may change)
watch(
  () => scopeStore.state.status.channels,
  () => {
    if (scopeStore.state.status.state === ScopeState.DONE) {
      createPlot();
    }
  },
);

onMounted(() => {
  createPlot();
  resizeObs = new ResizeObserver(() => {
    if (chartEl.value && plot) {
      const r = chartEl.value.getBoundingClientRect();
      plot.setSize({ width: Math.max(r.width, 200), height: Math.max(r.height, 150) });
    }
  });
  if (chartEl.value) resizeObs.observe(chartEl.value);
});

onBeforeUnmount(() => {
  resizeObs?.disconnect();
  plot?.destroy();
});
</script>

<template>
  <div class="scope-chart" ref="chartEl"></div>
</template>

<style scoped>
.scope-chart {
  width: 100%;
  height: 100%;
  min-height: 300px;
  background: #111;
  border: 1px solid #333;
  border-radius: 4px;
  overflow: hidden;
}

/* uPlot dark theme overrides */
.scope-chart :deep(.u-wrap) {
  background: #111;
}
.scope-chart :deep(.u-legend) {
  background: #1a1a1a;
  color: #ccc;
  font-size: 12px;
  padding: 4px 8px;
}
.scope-chart :deep(.u-legend .u-series) {
  padding: 0 8px;
}
</style>
