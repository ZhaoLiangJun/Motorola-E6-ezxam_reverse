// Ghidra headless script to export decompiled C for all functions
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Program;
import ghidra.program.model.symbol.SymbolTable;
import ghidra.program.model.address.Address;
import java.io.*;

public class ExportDecompiled extends GhidraScript {
    @Override
    public void run() throws Exception {
        String outDir = System.getProperty("user.dir") + File.separator + "decompiled";
        File dir = new File(outDir);
        if (!dir.exists()) dir.mkdirs();
        
        DecompInterface decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);
        
        // Write all functions decompiled
        FileWriter allW = new FileWriter(new File(dir, "all_decompiled.c"));
        FunctionIterator funcs = currentProgram.getFunctionManager().getFunctions(true);
        int count = 0;
        while (funcs.hasNext() && count < 3000) {
            Function f = funcs.next();
            if (f.isExternal()) continue;
            DecompileResults res = decompiler.decompileFunction(f, 60, monitor);
            if (res != null && res.decompileCompleted()) {
                String c = res.getDecompiledFunction().getC();
                String name = f.getName();
                String safeName = name.replaceAll("[^a-zA-Z0-9_]", "_");
                allW.write("/* ==== " + name + " @ " + f.getEntryPoint() + " ==== */\n");
                allW.write(c);
                allW.write("\n\n");
                
                // Per-function file
                try (FileWriter fw = new FileWriter(new File(dir, "fn_" + safeName + ".c"))) {
                    fw.write("/* ==== " + name + " @ " + f.getEntryPoint() + " ==== */\n");
                    fw.write(c);
                }
                count++;
            }
        }
        allW.close();
        println("Exported " + count + " functions to " + outDir);
    }
}
