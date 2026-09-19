@echo off
rem Use lupdate from the Qt installation selected in PATH.
lupdate "%~dp0../../app.pro" -no-obsolete
