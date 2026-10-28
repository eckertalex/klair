vim.opt.makeprg = "./check.sh"

vim.opt.errorformat = {
	"%f(%l:%c) Error: %m",
	"%f(%l:%c) Warning: %m",
	"%-G%.%#",
}
