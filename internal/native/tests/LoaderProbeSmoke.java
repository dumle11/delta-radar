/** Verify the comparison probe through normal ART loading in its own process. */
public final class LoaderProbeSmoke {
    public static void main(String[] args) {
        if (args.length != 1) throw new IllegalArgumentException("Expected probe library path");
        System.load(args[0]);
        System.out.println("PASS: probe loaded through ART and JNI_OnLoad returned a supported version");
    }
}
