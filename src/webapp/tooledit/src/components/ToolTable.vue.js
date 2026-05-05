import { ref } from 'vue';
import { toolStore } from '../stores/tools';
import ToolEditDialog from './ToolEditDialog.vue';
const columns = [
    { key: 'toolno', label: 'Tool', type: 'int' },
    { key: 'pocketno', label: 'Poc', type: 'int' },
    { key: 'x_offset', label: 'X', type: 'float' },
    { key: 'y_offset', label: 'Y', type: 'float' },
    { key: 'z_offset', label: 'Z', type: 'float' },
    { key: 'a_offset', label: 'A', type: 'float' },
    { key: 'b_offset', label: 'B', type: 'float' },
    { key: 'c_offset', label: 'C', type: 'float' },
    { key: 'u_offset', label: 'U', type: 'float' },
    { key: 'v_offset', label: 'V', type: 'float' },
    { key: 'w_offset', label: 'W', type: 'float' },
    { key: 'diameter', label: 'Diam', type: 'float' },
    { key: 'frontangle', label: 'Front', type: 'float' },
    { key: 'backangle', label: 'Back', type: 'float' },
    { key: 'orientation', label: 'Orient', type: 'int' },
    { key: 'comment', label: 'Comment', type: 'text' },
];
const dialogTool = ref(null);
const dialogIsNew = ref(false);
function editTool(tool) {
    dialogTool.value = { ...tool };
    dialogIsNew.value = false;
}
function addTool() {
    dialogTool.value = {
        toolno: 0, pocketno: 0,
        x_offset: 0, y_offset: 0, z_offset: 0,
        a_offset: 0, b_offset: 0, c_offset: 0,
        u_offset: 0, v_offset: 0, w_offset: 0,
        diameter: 0, frontangle: 0, backangle: 0,
        orientation: 0, comment: '',
    };
    dialogIsNew.value = true;
}
function onDialogSave(tool) {
    toolStore.saveTool(tool);
    dialogTool.value = null;
}
function onDialogCancel() {
    dialogTool.value = null;
}
function deleteTool(tool, event) {
    event.stopPropagation();
    if (confirm(`Delete tool ${tool.toolno}?`)) {
        toolStore.deleteTool(tool.toolno);
    }
}
function fmtNum(val, type) {
    if (type === 'text')
        return String(val);
    const n = Number(val);
    if (n === 0)
        return '0';
    if (type === 'int')
        return String(n);
    return n.toFixed(4).replace(/0+$/, '').replace(/\.$/, '');
}
const __VLS_exposed = { addTool };
defineExpose(__VLS_exposed);
debugger; /* PartiallyEnd: #3632/scriptSetup.vue */
const __VLS_ctx = {};
let __VLS_components;
let __VLS_directives;
/** @type {__VLS_StyleScopedClasses['btn-add']} */ ;
/** @type {__VLS_StyleScopedClasses['int']} */ ;
/** @type {__VLS_StyleScopedClasses['float']} */ ;
/** @type {__VLS_StyleScopedClasses['text']} */ ;
/** @type {__VLS_StyleScopedClasses['col-actions']} */ ;
/** @type {__VLS_StyleScopedClasses['btn-edit']} */ ;
/** @type {__VLS_StyleScopedClasses['btn-del']} */ ;
// CSS variable injection 
// CSS variable injection end 
__VLS_asFunctionalElement(__VLS_intrinsicElements.div, __VLS_intrinsicElements.div)({
    ...{ class: "table-wrapper" },
});
__VLS_asFunctionalElement(__VLS_intrinsicElements.div, __VLS_intrinsicElements.div)({
    ...{ class: "toolbar" },
});
__VLS_asFunctionalElement(__VLS_intrinsicElements.button, __VLS_intrinsicElements.button)({
    ...{ onClick: (__VLS_ctx.addTool) },
    ...{ class: "btn-add" },
});
__VLS_asFunctionalElement(__VLS_intrinsicElements.div, __VLS_intrinsicElements.div)({
    ...{ class: "table-scroll" },
});
__VLS_asFunctionalElement(__VLS_intrinsicElements.table, __VLS_intrinsicElements.table)({});
__VLS_asFunctionalElement(__VLS_intrinsicElements.thead, __VLS_intrinsicElements.thead)({});
__VLS_asFunctionalElement(__VLS_intrinsicElements.tr, __VLS_intrinsicElements.tr)({});
for (const [col] of __VLS_getVForSourceType((__VLS_ctx.columns))) {
    __VLS_asFunctionalElement(__VLS_intrinsicElements.th, __VLS_intrinsicElements.th)({
        key: (col.key),
        ...{ class: (col.type) },
    });
    (col.label);
}
__VLS_asFunctionalElement(__VLS_intrinsicElements.th, __VLS_intrinsicElements.th)({
    ...{ class: "col-actions" },
});
__VLS_asFunctionalElement(__VLS_intrinsicElements.tbody, __VLS_intrinsicElements.tbody)({});
for (const [tool] of __VLS_getVForSourceType((__VLS_ctx.toolStore.state.tools))) {
    __VLS_asFunctionalElement(__VLS_intrinsicElements.tr, __VLS_intrinsicElements.tr)({
        key: (tool.toolno),
    });
    for (const [col] of __VLS_getVForSourceType((__VLS_ctx.columns))) {
        __VLS_asFunctionalElement(__VLS_intrinsicElements.td, __VLS_intrinsicElements.td)({
            key: (col.key),
            ...{ class: (col.type) },
        });
        (__VLS_ctx.fmtNum(tool[col.key], col.type));
    }
    __VLS_asFunctionalElement(__VLS_intrinsicElements.td, __VLS_intrinsicElements.td)({
        ...{ class: "col-actions" },
    });
    __VLS_asFunctionalElement(__VLS_intrinsicElements.button, __VLS_intrinsicElements.button)({
        ...{ onClick: (...[$event]) => {
                __VLS_ctx.editTool(tool);
            } },
        ...{ class: "btn-edit" },
        title: "Edit",
    });
    __VLS_asFunctionalElement(__VLS_intrinsicElements.button, __VLS_intrinsicElements.button)({
        ...{ onClick: (...[$event]) => {
                __VLS_ctx.deleteTool(tool, $event);
            } },
        ...{ class: "btn-del" },
        title: "Delete",
    });
}
if (__VLS_ctx.toolStore.state.tools.length === 0) {
    __VLS_asFunctionalElement(__VLS_intrinsicElements.div, __VLS_intrinsicElements.div)({
        ...{ class: "empty" },
    });
}
if (__VLS_ctx.dialogTool) {
    /** @type {[typeof ToolEditDialog, ]} */ ;
    // @ts-ignore
    const __VLS_0 = __VLS_asFunctionalComponent(ToolEditDialog, new ToolEditDialog({
        ...{ 'onSave': {} },
        ...{ 'onCancel': {} },
        tool: (__VLS_ctx.dialogTool),
        isNew: (__VLS_ctx.dialogIsNew),
    }));
    const __VLS_1 = __VLS_0({
        ...{ 'onSave': {} },
        ...{ 'onCancel': {} },
        tool: (__VLS_ctx.dialogTool),
        isNew: (__VLS_ctx.dialogIsNew),
    }, ...__VLS_functionalComponentArgsRest(__VLS_0));
    let __VLS_3;
    let __VLS_4;
    let __VLS_5;
    const __VLS_6 = {
        onSave: (__VLS_ctx.onDialogSave)
    };
    const __VLS_7 = {
        onCancel: (__VLS_ctx.onDialogCancel)
    };
    var __VLS_2;
}
/** @type {__VLS_StyleScopedClasses['table-wrapper']} */ ;
/** @type {__VLS_StyleScopedClasses['toolbar']} */ ;
/** @type {__VLS_StyleScopedClasses['btn-add']} */ ;
/** @type {__VLS_StyleScopedClasses['table-scroll']} */ ;
/** @type {__VLS_StyleScopedClasses['col-actions']} */ ;
/** @type {__VLS_StyleScopedClasses['col-actions']} */ ;
/** @type {__VLS_StyleScopedClasses['btn-edit']} */ ;
/** @type {__VLS_StyleScopedClasses['btn-del']} */ ;
/** @type {__VLS_StyleScopedClasses['empty']} */ ;
var __VLS_dollars;
const __VLS_self = (await import('vue')).defineComponent({
    setup() {
        return {
            toolStore: toolStore,
            ToolEditDialog: ToolEditDialog,
            columns: columns,
            dialogTool: dialogTool,
            dialogIsNew: dialogIsNew,
            editTool: editTool,
            addTool: addTool,
            onDialogSave: onDialogSave,
            onDialogCancel: onDialogCancel,
            deleteTool: deleteTool,
            fmtNum: fmtNum,
        };
    },
});
export default (await import('vue')).defineComponent({
    setup() {
        return {
            ...__VLS_exposed,
        };
    },
});
; /* PartiallyEnd: #4569/main.vue */
