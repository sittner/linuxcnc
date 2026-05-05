import { reactive } from 'vue';
import { ToolsClient } from '../generated/tools_client';
const client = new ToolsClient(window.location.origin);
const state = reactive({
    tools: [],
    loading: false,
    error: null,
});
async function loadTools() {
    state.loading = true;
    state.error = null;
    try {
        state.tools = await client.listTools();
    }
    catch (e) {
        state.error = e instanceof Error ? e.message : String(e);
    }
    finally {
        state.loading = false;
    }
}
async function saveTool(tool) {
    state.error = null;
    try {
        await client.putTool(tool.toolno, tool);
        await loadTools();
    }
    catch (e) {
        state.error = e instanceof Error ? e.message : String(e);
    }
}
async function deleteTool(toolno) {
    state.error = null;
    try {
        await client.deleteTool(toolno);
        state.tools = state.tools.filter(t => t.toolno !== toolno);
    }
    catch (e) {
        state.error = e instanceof Error ? e.message : String(e);
    }
}
async function reloadTable() {
    state.error = null;
    try {
        await client.reloadTools();
        await loadTools();
    }
    catch (e) {
        state.error = e instanceof Error ? e.message : String(e);
    }
}
export const toolStore = {
    state,
    loadTools,
    saveTool,
    deleteTool,
    reloadTable,
};
