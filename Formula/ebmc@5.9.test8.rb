class Ebmc < Formula
  desc "Model Checker for SystemVerilog"
  homepage "https://www.cprover.org/ebmc/"
  url "https://github.com/diffblue/hw-cbmc.git",
    tag: "refs/tags/ebmc-5.9.test8",
    revision: "89d13dc68e77b056a1c01080ce4a5e59dfb6e93b"
  version "5.8"
  license "BSD-3-Clause"

  uses_from_macos "flex" => :build
  uses_from_macos "curl" => :build
  depends_on "bison" => :build

  def install
    system "make", "-C", "lib/cbmc/src", "minisat2-download"
    system "make", "-C", "src"
    system "mkdir", "-p", "#{prefix}/usr/bin"
    system "cp", "src/ebmc/ebmc", "#{prefix}/usr/bin/"
  end

  test do
    system "make", "-C", "regression/ebmc", "test"
  end
end
