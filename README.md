# Custom Card Checker for Project Ignis #
Rewrite of [the official check suite](https://github.com/ProjectIgnis/Checker). Currently only performs basic syntax check just as the original one but more features should be added in the future (as in the original one...).

Does not serve as a demo for the API I think.

## Script Syntax Checker ##
Usage:
`script_syntax_check <directory> <card id>`

The specified directory, including subdirectories, is searched for any card- and utility-script files as well as databases. If multiple files with the same name occur the one with the newest editing-date is used.

All utility scripts are loaded into the core, then all card-scripts are loaded with the same passcodes (## TODO: actually use the stats loaded from the cdbs ##). Three-digit passcodes and 151000000 (Action Duel script) are currently not skipped, should be added?

## Caveats ##
Currently has the same caveats as the original one (the file system one just because I do not have another System at hand...)

## Copyright notice and license

Copyright (C) 2020  Kevin Lu, rewritten by 0x4261756D (https://github.com/0x4261756D). (I do not know if this is the proper way to write this)
```
This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as published
by the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
```
