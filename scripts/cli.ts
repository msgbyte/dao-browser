import { Command } from "commander";
import { downloadCommand } from "./commands/download.js";
import { importCommand } from "./commands/import.js";
import { exportCommand } from "./commands/export.js";
import { buildCommand } from "./commands/build.js";
import { packageCommand } from "./commands/package.js";

const program = new Command();

program
  .name("dao")
  .description("Dao Browser build toolchain")
  .version("0.1.0");

program.addCommand(downloadCommand);
program.addCommand(importCommand);
program.addCommand(exportCommand);
program.addCommand(buildCommand);
program.addCommand(packageCommand);

program.parse();
