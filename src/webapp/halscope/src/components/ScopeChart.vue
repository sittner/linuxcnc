<script setup lang="ts">
import { ref, watch, onMounted, onBeforeUnmount } from 'vue';
import uPlot from 'uplot';
import 'uplot/dist/uPlot.min.css';
import { scopeStore } from '../stores/scope';
import { ScopeState } from '../generated/halscope_client';

const chartEl = ref<HTMLDivElement>();
let plot: uPlot | null = null;
let resizeObs: ResizeObserver | null = null;

function buildOpts(width: number, height: number): uPlot.Options {
  const channels = scopeStore.state.status.channels.filter(c => c.enabled);
  const series: uPlot.Series[] = [
    { label: 'Time (s)' },
    ...channels.map((ch) => ({
      label: ch.pinName || `Ch ${ch.channel}`,
      stroke: scopeStore.channelUI[ch.channel]?.color ?? '#ffff00',
      width: 1.5,
      show: scopeStore.channelUI[ch.channel]?.visible ?? true,
    })),
  ];

  return {
    width,
    height,
    cursor: { drag: { x: true, y: true } },
    scales: {
      x: { time: false },
    },
    axes: [
      {
        label: 'Time (s)',
        stroke: '#888',
        grid: { stroke: '#333', width: 1 },
        ticks: { stroke: '#444', width: 1 },
      },
      {
        label: 'Value',
        stroke: '#888',
        grid: { stroke: '#333', width: 1 },
        ticks: { stroke: '#444', width: 1 },
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

  // Use the original display window calculation for sample slicing
  const dw = scopeStore.calcDisplayWindow();
  const total = st.timeBase.length;
  const i0 = Math.max(0, dw.startSample);
  const i1 = Math.min(total, dw.endSample + 1);

  const time = Array.from(st.timeBase.subarray(i0, i1)) as unknown as number[];
  const data: (number[] | Float64Array)[] = [time];

  const channels = st.status.channels.filter(c => c.enabled);
  for (const ch of channels) {
    const s = st.samples.find(s => s.channel === ch.channel);
    if (s) {
      data.push(s.data.subarray(i0, i1));
    } else {
      data.push(new Float64Array(i1 - i0));
    }
  }

  return data as uPlot.AlignedData;
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

  const data = buildData();
  const channels = scopeStore.state.status.channels.filter(c => c.enabled);

  // If channel count changed, rebuild the plot
  if (channels.length + 1 !== plot.series.length) {
    createPlot();
    return;
  }

  plot.setData(data);
}

// Watch for sample data changes
watch(
  () => scopeStore.state.samples,
  () => updateData(),
);

// Watch for view window changes (pan/zoom from buffer indicator)
watch(
  () => [scopeStore.state.zoomSetting, scopeStore.state.posSetting],
  () => updateData(),
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
